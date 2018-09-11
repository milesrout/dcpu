#define _DEFAULT_SOURCE

#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

#include "types.h"
#include "utils.h"
#include "exception.h"
#include "dcpu.h"
#include "lem1802.h"
#include "dfpu17.h"

const struct dcpu dcpu_init = {0};

u16 *DIRTY(u16 d)
{
	u16 *p = emalloc(sizeof *p);
	*p = d;
	return p;
}

void noop_interrupt(struct hardware *hw, struct dcpu *dcpu)
{
	(void)hw;
	(void)dcpu;
}

void noop_cycle(struct hardware *hw, u16 *dirty, struct dcpu *dcpu)
{
	(void)hw;
	(void)dirty;
	(void)dcpu;
}

struct hardware *nth_hardware(struct dcpu *dcpu, u16 n)
{
	if (n >= dcpu->hw_count)
		return NULL;

	return dcpu->hw + n;
}

static u16 *decode_b(struct dcpu *dcpu, u16 b, u16 **dirty)
{
	#define NEXTWORD dcpu->ram[dcpu->pc++]
	switch (b) {
		case 0x00: case 0x01: case 0x02: case 0x03:
		case 0x04: case 0x05: case 0x06: case 0x07:
			*dirty = NULL;
			return &dcpu->registers[b];
		case 0x08: case 0x09: case 0x0a: case 0x0b:
		case 0x0c: case 0x0d: case 0x0e: case 0x0f:
			*dirty = DIRTY(dcpu->registers[b - 0x08]);
			return &dcpu->ram[**dirty];
		case 0x10: case 0x11: case 0x12: case 0x13:
		case 0x14: case 0x15: case 0x16: case 0x17:
			dcpu->cycles++;
			*dirty = DIRTY(dcpu->registers[b - 0x10] + NEXTWORD);
			return &dcpu->ram[**dirty];
		case 0x18:
			*dirty = DIRTY(--dcpu->sp);
			return &dcpu->ram[**dirty];
		case 0x19:
			*dirty = DIRTY(dcpu->sp);
			return &dcpu->ram[**dirty];
		case 0x1a:
			dcpu->cycles++;
			*dirty = DIRTY(dcpu->sp + NEXTWORD);
			return &dcpu->ram[**dirty];
		case 0x1b:
			*dirty = NULL;
			return &dcpu->sp;
		case 0x1c:
			*dirty = NULL;
			return &dcpu->pc;
		case 0x1d:
			*dirty = NULL;
			return &dcpu->ex;
		case 0x1e:
			dcpu->cycles++;
			*dirty = DIRTY(NEXTWORD);
			return &dcpu->ram[**dirty];
		case 0x1f:
			dcpu->cycles++;
			*dirty = NULL;
			return &NEXTWORD;
		default:
			throw("decode_b", "out of range");
	}
	#undef NEXTWORD

	/* unreachable */
	abort();
}

static u16 const *decode_b_nomut(struct dcpu *dcpu, u16 b)
{
	u16 *dirty_unused;
	if (b == 0x18)
		return &dcpu->ram[dcpu->sp - 1];
	return decode_b(dcpu, b, &dirty_unused);
}

static u16 decode_a(struct dcpu *dcpu, u16 a)
{
	u16 *dirty_unused;
	if (a >= 0x40) throw("decode_a", "too large");
	if (a == 0x18) return dcpu->ram[dcpu->sp++];
	if (a < 0x20) return *decode_b(dcpu, a, &dirty_unused);

	/* 0x20-0x3f | literal value 0xffff-0x1e (-1..30) (literal) (only for a) */
	return a - 0x21;
}

static u16 decode_a_nomut(struct dcpu *dcpu, u16 a)
{
	if (a == 0x18) return dcpu->ram[dcpu->sp];
	if (a < 0x20) return *decode_b_nomut(dcpu, a);
	return decode_a(dcpu, a);
}

static u16 interrupt(struct dcpu *dcpu, u16 message)
{
	(void)dcpu;
	(void)message;
	throw("interrupt", "not yet implemented");

	/* if we get here, throw is broken */
	abort();
}

static u16 *instr_cycle(struct dcpu *dcpu)
{
	u16 instruction = dcpu->ram[dcpu->pc++];
	u16 opcode = instruction & 0x001f;
	u16 enc_b = (instruction & 0x03e0) >> 5;
	u16 enc_a = (instruction & 0xfc00) >> 10;
	u16 *dirty = NULL;

	/*
	if (opcode == 0x00) {
		fprintf(stderr, "\ninstr=0x%04x a=0x%02x o=0x%02x        | ", 
				instruction, enc_a, enc_b);
	} else {
		fprintf(stderr, "\ninstr=0x%04x a=0x%02x b=0x%02x o=0x%02x | ", 
				instruction, enc_a, enc_b, opcode);
	}
	*/
	
	dcpu->cycles++;

	if (dcpu->skipping) {
		/**
		 * we still have to decode the operands so that the instruction
		 * takes up the right number of cycles and consumes the right
		 * number of 'next word's.
		 */
		if (opcode == 0x00) {
			decode_b_nomut(dcpu, enc_a);
		} else {
			decode_a_nomut(dcpu, enc_a);
			decode_b_nomut(dcpu, enc_b);
		}

		/* IF chaining */
		if (0x10 <= opcode && opcode < 0x18)
			return NULL;

		dcpu->skipping = 0;
		return NULL;
	}

	if (opcode == 0x00) {
		/* u16   a = decode_a(dcpu, enc_a); */
		u16 *pa = decode_b(dcpu, enc_a, &dirty);
		/* printf("b=%04x\n", enc_b); */
		switch (enc_b) {
		case 0x00:
			/* BREAK */
			fprintf(stderr, " A:0x%04x  B:0x%04x  C:0x%04x  I:0x%04x\n",
				dcpu->registers[0], dcpu->registers[1],
				dcpu->registers[2], dcpu->registers[6]);
			fprintf(stderr, " X:0x%04x  Y:0x%04x  Z:0x%04x  J:0x%04x\n",
				dcpu->registers[3], dcpu->registers[4],
				dcpu->registers[5], dcpu->registers[7]);
			fprintf(stderr, "PC:0x%04x SP:0x%04x EX:0x%04x IA:0x%04x\n",
				dcpu->pc, dcpu->sp, dcpu->ex, dcpu->ia);
			getchar();
			return NULL;
		case 0x01:
			dcpu->ram[--dcpu->sp] = dcpu->pc;
			dcpu->pc = *pa;
			dcpu->cycles += 2;
			return DIRTY(dcpu->sp);
		case 0x02:
			/* TRACE */
			fprintf(stderr, " A:0x%04x  B:0x%04x  C:0x%04x  I:0x%04x\n",
				dcpu->registers[0], dcpu->registers[1],
				dcpu->registers[2], dcpu->registers[6]);
			fprintf(stderr, " X:0x%04x  Y:0x%04x  Z:0x%04x  J:0x%04x\n",
				dcpu->registers[3], dcpu->registers[4],
				dcpu->registers[5], dcpu->registers[7]);
			fprintf(stderr, "PC:0x%04x SP:0x%04x EX:0x%04x IA:0x%04x\n",
				dcpu->pc, dcpu->sp, dcpu->ex, dcpu->ia);
			return NULL;
		case 0x08:
			dcpu->cycles += 3;
			return DIRTY(interrupt(dcpu, *pa));
		case 0x09:
			*pa = dcpu->ia;
			return dirty;
		case 0x0a:
			dcpu->ia = *pa;
			return NULL;
		case 0x0b:
			dcpu->queue_interrupts = false;
			dcpu->registers[0] = dcpu->ram[dcpu->sp++];
			dcpu->pc = dcpu->ram[dcpu->sp++];
			dcpu->cycles += 2;
			return NULL;
		case 0x0c:
			dcpu->queue_interrupts = (*pa != 0);
			dcpu->cycles++;
			return NULL;
		case 0x10:
		{
			*pa = dcpu->hw_count;
			dcpu->cycles++;
			return dirty;
		}
		case 0x11:
		{
			struct device *device = nth_hardware(dcpu, *pa)->device;
			if (device != NULL) {
				dcpu->registers[0] = device->id;
				dcpu->registers[1] = device->id >> 16;
				dcpu->registers[2] = device->version;
				dcpu->registers[3] = device->manufacturer;
				dcpu->registers[4] = device->manufacturer >> 16;
				fprintf(stderr, "A:0x%04x B:0x%04x C:0x%04x X:0x%04x Y:0x%04x\n",
					dcpu->registers[0],
					dcpu->registers[1],
					dcpu->registers[2],
					dcpu->registers[3],
					dcpu->registers[4]);
			}
			dcpu->cycles += 3;
			return NULL;
		}
		case 0x12:
		{
			struct hardware *hw = nth_hardware(dcpu, *pa);
			if (hw != NULL && hw->device->interrupt != NULL) {
				hw->device->interrupt(hw, dcpu);
			}
			dcpu->cycles += 3;

			/* todo: determine how to deal with hardware devices
			 * modifying DCPU-16 memory
			 */
			return NULL;
		}
		default: throw("unaryopcode", "out of range");
		}
	} else {
		u16  a = decode_a(dcpu, enc_a);
		u16 *b = decode_b(dcpu, enc_b, &dirty);
#if 0
		fprintf(stderr, "dirty: %p", dirty);
		if (dirty)
			fprintf(stderr, ", 0x%04x\n", *dirty);
		else
			fprintf(stderr, "\n");
#endif
		if (opcode == 0x01) {
			*b = a;
		} else if (opcode == 0x02) {
			u32 c    = (u32)a + (u32)*b;
			dcpu->ex = c >> 16;
			*b       = c;
			dcpu->cycles++;
		} else if (opcode == 0x03) {
			u32 c    = (u32)*b - (u32)a;
			dcpu->ex = c >> 16;
			*b       = c;
			dcpu->cycles++;
		} else if (opcode == 0x04) {
			u32 c    = (u32)*b * (u32)a;
			dcpu->ex = c >> 16;
			*b       = c;
			dcpu->cycles++;
		} else if (opcode == 0x05) {
			s32 c    = (s32)(s16)*b * (s32)(s16)a;
			dcpu->ex = (u32)c >> 16;
			*b       = (u32)c;
			dcpu->cycles++;
		} else if (opcode == 0x06) {
			if (a == 0) {
				dcpu->ex = 0;
				*b       = 0;
			} else {
				u32 c = (u32)*b / (u32)a;
				dcpu->ex = c >> 16;
				*b       = c;
			}
			dcpu->cycles += 2;
		} else if (opcode == 0x07) {
			if (a == 0) {
				dcpu->ex = 0;
				*b       = 0;
			} else {
				s32 c = (s32)(s16)*b / (s32)(s16)a;
				dcpu->ex = (u32)c >> 16;
				*b       = (u32)c;
			}
			dcpu->cycles += 2;
		} else if (opcode == 0x08) {
			if (a == 0)
				*b = 0;
			else
				*b = *b % a;
			dcpu->cycles += 2;
		} else if (opcode == 0x09) {
			if (a == 0) {
				*b = 0;
			} else {
				u16 c = *b % a;
				*b = (s16)*b < 0 ? -(s16)c : c;
			}
			dcpu->cycles += 2;
		} else if (opcode == 0x0a) {
			*b = *b & a;
		} else if (opcode == 0x0b) {
			*b = *b | a;
		} else if (opcode == 0x0c) {
			*b = *b ^ a;
		} else if (opcode == 0x0d) {
			u32 c    = *b >> a;
			dcpu->ex = c >> 16;
			*b       = c;
		} else if (opcode == 0x0e) {
			s32 c    = (s16)*b >> a;
			dcpu->ex = (u16)(c >> 16);
			*b       = (u16)c;
		} else if (opcode == 0x0f) {
			*b = *b << a;
		} else if (opcode == 0x10) {
			dcpu->cycles++;
			if (!((*b & a) != 0))
				dcpu->cycles++, dcpu->skipping = 1;
			return NULL;
		} else if (opcode == 0x11) {
			dcpu->cycles++;
			if (!((*b & a) == 0))
				dcpu->cycles++, dcpu->skipping = 1;
			return NULL;
		} else if (opcode == 0x12) {
			dcpu->cycles++;
			if (!(*b == a))
				dcpu->cycles++, dcpu->skipping = 1;
			return NULL;
		} else if (opcode == 0x13) {
			dcpu->cycles++;
			if (!(*b != a))
				dcpu->cycles++, dcpu->skipping = 1;
			return NULL;
		} else if (opcode == 0x14) {
			dcpu->cycles++;
			if (!(*b > a))
				dcpu->cycles++, dcpu->skipping = 1;
			return NULL;
		} else if (opcode == 0x15) {
			dcpu->cycles++;
			if (!((s16)*b > (s16)a))
				dcpu->cycles++, dcpu->skipping = 1;
			return NULL;
		} else if (opcode == 0x16) {
			dcpu->cycles++;
			if (!(*b < a))
				dcpu->cycles++, dcpu->skipping = 1;
			return NULL;
		} else if (opcode == 0x17) {
			dcpu->cycles++;
			if (!((s16)*b < (s16)a))
				dcpu->cycles++, dcpu->skipping = 1;
			return NULL;
		} else if (opcode == 0x18) {
			fprintf(stderr, "0x%04x: ", opcode);
			throw("binaryopcode", "out of range");
		} else if (opcode == 0x19) {
			fprintf(stderr, "0x%04x: ", opcode);
			throw("binaryopcode", "out of range");
		} else if (opcode == 0x1a) {
			u32 c    = (u32)*b + (u32)a + (u32)dcpu->ex;
			dcpu->ex = c >> 16;
			*b       = c;
			dcpu->cycles += 2;
		} else if (opcode == 0x1b) {
			u32 c    = (u32)*b - (u32)a + (u32)dcpu->ex;
			dcpu->ex = c >> 16;
			*b       = c;
			dcpu->cycles += 2;
		} else if (opcode == 0x1c) {
			fprintf(stderr, "0x%04x: ", opcode);
			throw("binaryopcode", "out of range");
		} else if (opcode == 0x1d) {
			fprintf(stderr, "0x%04x: ", opcode);
			throw("binaryopcode", "out of range");
		} else if (opcode == 0x1e) {
			*b = a;
			dcpu->registers[6]++;
			dcpu->registers[7]++;
			dcpu->cycles++;
		} else if (opcode == 0x1f) {
			*b = a;
			dcpu->registers[6]--;
			dcpu->registers[7]--;
			dcpu->cycles++;
		} else {
			throw("binaryopcode", "out of range");
		}
	}
	return dirty;
}

static void cycle(struct dcpu *dcpu)
{
	u16 *dirty;
	int i;
	
	dirty = instr_cycle(dcpu);

	for (i = 0; i < dcpu->hw_count; i++)
		dcpu->hw[i].device->cycle(dcpu->hw + i, dirty, dcpu);
}

u16 programme[] = {
#include "../examples/mandelbrot.hex"
};

#define TIMESLICE 0.01
#define CLOCKRATE 100000

static double diffclock(struct timespec b, struct timespec a)
{
	return (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1000000000.0;
}

static double inseconds(struct timespec a)
{
	return a.tv_sec + a.tv_nsec / 1000000000.0;
}

int main()
{
	struct dcpu dcpu = DCPU_INIT;
	struct timespec last_start, timeslice_start, current;
	int last_start_cycles, timeslice_start_cycles;
	
	dcpu.quirks = 0;
	/* turn me on if the program you are testing requires that the monitor
	 * is automatically turned on at the beginning of execution and mapped
	 * to 0x8000.
	 */
	/* dcpu.quirks |= DCPU_QUIRKS_LEM1802_ALWAYS_ON; */

	/* turn me on if the program you are testing can use a 24-bit colour
	 * palette. (rrrrrggggggbbbbb instead of 0000rrrrggggbbbb).
	 */
	/* dcpu.quirks |= DCPU_QUIRKS_LEM1802_USE_16BIT_COLOUR; */

	memcpy(dcpu.ram, programme, sizeof programme);

	dcpu.hw_count = 5;
	dcpu.hw = emalloc(5 * sizeof(struct hardware));

	dcpu.hw[0].device = make_lem1802(&dcpu);

	dcpu.hw[1].device = make_dfpu17(&dcpu);

	{
		struct device d = {0x30cf7406, 0x0001, 0x90099009, NULL, &noop_interrupt, &noop_cycle};
		dcpu.hw[2].device = emalloc(sizeof(struct device));
		*dcpu.hw[2].device = d;
	}
	{
		struct device d = {0x12d0b402, 0x0001, 0x90099009, NULL, &noop_interrupt, &noop_cycle};
		dcpu.hw[3].device = emalloc(sizeof(struct device));
		*dcpu.hw[3].device = d;
	}
	{
		struct device d = {0x74fa4cae, 0x07c2, 0x21544948, NULL, &noop_interrupt, &noop_cycle};
		dcpu.hw[4].device = emalloc(sizeof(struct device));
		*dcpu.hw[4].device = d;
	}

	clock_gettime(CLOCK_MONOTONIC, &last_start);
	timeslice_start = last_start;
	last_start_cycles = timeslice_start_cycles = dcpu.cycles;

	puts("");
label:
	if (!setjmp(except_buf)) {
		while (1) {
			clock_gettime(CLOCK_MONOTONIC, &current);

#if 0
			if (dcpu.cycles - timeslice_start_cycles > CLOCKRATE * TIMESLICE) {
				if (!(dcpu.cycles - last_start_cycles > CLOCKRATE * diffclock(current, last_start))) {
					fprintf(stderr, "a");
				}
			}
			if (dcpu.cycles - last_start_cycles > CLOCKRATE * diffclock(current, last_start)) {
				if (!(dcpu.cycles - timeslice_start_cycles > CLOCKRATE * TIMESLICE)) {
					fprintf(stderr, "b");
				}
			}
#endif

			if (dcpu.cycles - timeslice_start_cycles > CLOCKRATE * TIMESLICE
				&& dcpu.cycles - last_start_cycles > CLOCKRATE * diffclock(current, last_start)) {
				double sleeptime = diffclock(timeslice_start, current) + TIMESLICE;
#if 0
				fprintf(stderr, "cc=%d, tsc=%d, lsc=%d\n", dcpu.cycles, timeslice_start_cycles, last_start_cycles);
				fprintf(stderr, "c=%f, ts=%f, ls=%f\n",
						inseconds(current),
						inseconds(timeslice_start),
						inseconds(last_start));
				fprintf(stderr, "ets=%f, rem=%f\n",
					inseconds(timeslice_start) + TIMESLICE,
					inseconds(timeslice_start) + TIMESLICE - inseconds(current));
				fprintf(stderr, "ec=%f\n", CLOCKRATE * diffclock(current, last_start));
				
#endif
				if (sleeptime > 0) {
					/* fprintf(stderr, "sleeping for %fms\n", 1000 * sleeptime); */
					usleep(1000000.0 * sleeptime);
				}
				clock_gettime(CLOCK_MONOTONIC, &current);

#if 0
				fprintf(stderr, "c=%f, ls=%f\n",
					inseconds(current),
					inseconds(last_start));
				fprintf(stderr, "%d cycles in %fs = %f Hz.\n",
					dcpu.cycles - last_start_cycles,
					diffclock(current, last_start),
					1.0 * (dcpu.cycles - last_start_cycles) /
					diffclock(current, last_start));
#endif

				timeslice_start_cycles = dcpu.cycles;
				timeslice_start = current;
				continue;
			}

#if 0
			/* fprintf(stderr, "%ld\n", clock2);*/
			if (diffclock(current, timeslice_start) >= 0.01) {
				fprintf(stderr, "%d cycles per 10ms.                         \n",
					(dcpu.cycles - timeslice_start_cycles) / 5);
				timeslice_start_cycles = dcpu.cycles;
				timeslice_start = current;
			}
#endif

			cycle(&dcpu);
		}
	} else {
		fprintf(stderr, "%s: %s\n", except->desc, except->what);
		fprintf(stderr, " 0x%04x 0x%04x 0x%04x 0x%04x\n",
			dcpu.ram[110], dcpu.ram[111],
			dcpu.ram[112], dcpu.ram[113]);
		fprintf(stderr, " 0x%04x 0x%04x 0x%04x 0x%04x\n",
			dcpu.ram[512 + 110], dcpu.ram[512 + 111],
			dcpu.ram[512 + 112], dcpu.ram[512 + 113]);
		fprintf(stderr, " A:0x%04x  B:0x%04x  C:0x%04x  I:0x%04x\n",
			dcpu.registers[0], dcpu.registers[1],
			dcpu.registers[2], dcpu.registers[6]);
		fprintf(stderr, " X:0x%04x  Y:0x%04x  Z:0x%04x  J:0x%04x\n",
			dcpu.registers[3], dcpu.registers[4],
			dcpu.registers[5], dcpu.registers[7]);
		fprintf(stderr, "PC:0x%04x SP:0x%04x EX:0x%04x IA:0x%04x\n",
			dcpu.pc, dcpu.sp, dcpu.ex, dcpu.ia);
		abort();
		goto label;
	}
}
