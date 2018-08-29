#define _DEFAULT_SOURCE
#include <arpa/inet.h>
#include <unistd.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>

#include "types.h"
#include "utils.h"
#include "exception.h"
#include "dcpu.h"
#include "lem1802.h"

void getchar_interrupt(struct device *device, struct dcpu *dcpu)
{
	(void)dcpu;
	fprintf(stderr, "#id=0x%08x version=0x%04x man=0x%08x#",
		device->id, device->version, device->manufacturer);
	fprintf(stderr,
	       "a=0x%04x b=0x%04x c=0x%04x x=0x%04x "
	       "y=0x%04x z=0x%04x i=0x%04x j=0x%04x | "
	       "pc=0x%04x sp=0x%04x | cyc=%d skip=%d",
	       dcpu->registers[0], dcpu->registers[1],
	       dcpu->registers[2], dcpu->registers[3],
	       dcpu->registers[4], dcpu->registers[5],
	       dcpu->registers[6], dcpu->registers[7],
	       dcpu->pc, dcpu->sp, dcpu->cycles, dcpu->skipping);
	getchar();
}

#define get_member_of(type, value, member) (((type*)((value)->data))->member)

void noop_cycle(struct device *device, struct dcpu *dcpu)
{
	(void)device;
	(void)dcpu;
}

struct device *nth_device(struct dcpu *dcpu, u16 n)
{
	struct hardware *hw = dcpu->hw;
	int i;

	for (i = 0; i < n; i++) {
		if (hw) {
			hw = hw->next;
		} else {
			return NULL;
		}
	}
	if (hw)
		return hw->device;
	else
		return NULL;
}

static u16 *decode_b(struct dcpu *dcpu, u16 b)
{
	#define NEXTWORD dcpu->ram[dcpu->pc++]
	switch (b) {
		case 0x00: case 0x01: case 0x02: case 0x03:
		case 0x04: case 0x05: case 0x06: case 0x07:
			return &dcpu->registers[b];
		case 0x08: case 0x09: case 0x0a: case 0x0b:
		case 0x0c: case 0x0d: case 0x0e: case 0x0f:
			return &dcpu->ram[dcpu->registers[b - 0x08]];
		case 0x10: case 0x11: case 0x12: case 0x13:
		case 0x14: case 0x15: case 0x16: case 0x17:
			dcpu->cycles++;
			return &dcpu->ram[dcpu->registers[b - 0x10] + NEXTWORD];
		case 0x18:
			return &dcpu->ram[--dcpu->sp];
		case 0x19:
			return &dcpu->ram[dcpu->sp];
		case 0x1a:
			dcpu->cycles++;
			return &dcpu->ram[dcpu->sp + NEXTWORD];
		case 0x1b:
			return &dcpu->sp;
		case 0x1c:
			return &dcpu->pc;
		case 0x1d:
			return &dcpu->ex;
		case 0x1e:
			dcpu->cycles++;
			return &dcpu->ram[NEXTWORD];
		case 0x1f:
			dcpu->cycles++;
			return &NEXTWORD;
		default:
			throw("decode_b", "out of range");
	}
	#undef NEXTWORD
}

static u16 const *decode_b_nomut(struct dcpu *dcpu, u16 b)
{
	if (b == 0x18)
		return &dcpu->ram[dcpu->sp - 1];
	return decode_b(dcpu, b);
}

static u16 decode_a(struct dcpu *dcpu, u16 a)
{
	if (a >= 0x40) throw("decode_a", "too large");
	if (a == 0x18) return dcpu->ram[dcpu->sp++];
	if (a < 0x20) return *decode_b(dcpu, a);

	/* 0x20-0x3f | literal value 0xffff-0x1e (-1..30) (literal) (only for a) */
	return a - 0x21;
}

static u16 decode_a_nomut(struct dcpu *dcpu, u16 a)
{
	if (a == 0x18) return dcpu->ram[dcpu->sp];
	if (a < 0x20) return *decode_b_nomut(dcpu, a);
	return decode_a(dcpu, a);
}

static void interrupt(struct dcpu *dcpu, u16 message)
{
	(void)dcpu;
	(void)message;
	throw("interrupt", "not yet implemented");
}

static void instr_cycle(struct dcpu *dcpu)
{
	u16 instruction = dcpu->ram[dcpu->pc++];
	u16 opcode = instruction & 0b11111;
	u16 enc_b = (instruction & 0b1111100000) >> 5;
	u16 enc_a = (instruction & 0b1111110000000000) >> 10;

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
		if (opcode == 0x00) {
			decode_b_nomut(dcpu, enc_a);
		} else {
			decode_a_nomut(dcpu, enc_a);
			decode_b_nomut(dcpu, enc_b);
		}
		if (0x10 <= opcode && opcode < 0x18)
			return;

		dcpu->skipping = 0;
		return;
	}

	if (opcode == 0x00) {
		/* u16   a = decode_a(dcpu, enc_a); */
		u16 *pa = decode_b(dcpu, enc_a);
		/* printf("a=%04x [a]=%04x", a, *pa); */
		switch (enc_b) {
		case 0x01:
			dcpu->ram[--dcpu->sp] = dcpu->pc;
			dcpu->pc = *pa;
			dcpu->cycles += 2;
			break;
		case 0x08:
			interrupt(dcpu, *pa);
			dcpu->cycles += 3;
			break;
		case 0x09:
			*decode_b(dcpu, enc_a) = dcpu->ia;
			break;
		case 0x0a:
			dcpu->ia = *pa;
			break;
		case 0x0b:
			dcpu->queue_interrupts = false;
			dcpu->registers[0] = dcpu->ram[dcpu->sp++];
			dcpu->pc = dcpu->ram[dcpu->sp++];
			dcpu->cycles += 2;
			break;
		case 0x0c:
			dcpu->queue_interrupts = (*pa != 0);
			dcpu->cycles++;
			break;
		case 0x10:
		{
			u16 count = 0;
			struct hardware *hw = dcpu->hw;
			while (hw) {
				count++;
				hw = hw->next;
			}
			*pa = count;
			dcpu->cycles++;
			break;
		}
		case 0x11:
		{
			struct device *device = nth_device(dcpu, *pa);
			if (device != NULL) {
				dcpu->registers[0] = device->id;
				dcpu->registers[1] = device->id >> 16;
				dcpu->registers[2] = device->version;
				dcpu->registers[3] = device->manufacturer;
				dcpu->registers[4] = device->manufacturer >> 16;
			}
			dcpu->cycles += 3;
			break;
		}
		case 0x12:
		{
			struct device *device = nth_device(dcpu, *pa);
			if (device != NULL && device->interrupt != NULL) {
				device->interrupt(device, dcpu);
			}
			dcpu->cycles += 3;
			break;
		}
		default: throw("unaryopcode", "out of range");
		}
	} else {
		u16  a = decode_a(dcpu, enc_a);
		u16 *b = decode_b(dcpu, enc_b);
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
		} else if (opcode == 0x11) {
			dcpu->cycles++;
			if (!((*b & a) == 0))
				dcpu->cycles++, dcpu->skipping = 1;
		} else if (opcode == 0x12) {
			dcpu->cycles++;
			if (!(*b == a))
				dcpu->cycles++, dcpu->skipping = 1;
		} else if (opcode == 0x13) {
			dcpu->cycles++;
			if (!(*b != a))
				dcpu->cycles++, dcpu->skipping = 1;
		} else if (opcode == 0x14) {
			dcpu->cycles++;
			if (!(*b > a))
				dcpu->cycles++, dcpu->skipping = 1;
		} else if (opcode == 0x15) {
			dcpu->cycles++;
			if (!((s16)*b > (s16)a))
				dcpu->cycles++, dcpu->skipping = 1;
		} else if (opcode == 0x16) {
			dcpu->cycles++;
			if (!(*b < a))
				dcpu->cycles++, dcpu->skipping = 1;
		} else if (opcode == 0x17) {
			dcpu->cycles++;
			if (!((s16)*b < (s16)a))
				dcpu->cycles++, dcpu->skipping = 1;
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
}

static void cycle(struct dcpu *dcpu)
{
	struct hardware *hw = dcpu->hw;
	while (hw) {
		if (hw->device) {
			hw->device->cycle(hw->device, dcpu);
		}
		hw = hw->next;
	}
	instr_cycle(dcpu);
}

u16 diag[] = {
#include "../diagnostics.txt"
};

int main()
{
	struct dcpu dcpu = DCPU_INIT;
	/* dcpu.ram[0] = 0x7c01; */
	memcpy(dcpu.ram, diag, sizeof diag);

	dcpu.hw = emalloc(sizeof(struct hardware));
	dcpu.hw->device = make_lem1802();

	dcpu.hw->next = emalloc(sizeof(struct hardware));
	dcpu.hw->next->device = emalloc(sizeof(struct device));
	*dcpu.hw->next->device = DEVICE_INIT(0x30cf7406, 0x0001, 0x90099009);

	dcpu.hw->next->next = emalloc(sizeof(struct hardware));
	dcpu.hw->next->next->device = emalloc(sizeof(struct device));
	*dcpu.hw->next->next->device = DEVICE_INIT(0x12d0b402, 0x0001, 0x90099009);

	dcpu.hw->next->next->next = emalloc(sizeof(struct hardware));
	dcpu.hw->next->next->next->device = emalloc(sizeof(struct device));
	*dcpu.hw->next->next->next->device = DEVICE_INIT(0x74fa4cae, 0x07c2, 0x21544948);

	dcpu.hw->next->next->next->next = NULL;
label:
	if (!setjmp(except_buf)) {
		while (1) {
			cycle(&dcpu);
			/* fprintf(stderr,
			       "a=0x%04x b=0x%04x c=0x%04x x=0x%04x "
			       "y=0x%04x z=0x%04x i=0x%04x j=0x%04x | "
			       "pc=0x%04x sp=0x%04x | cyc=%d skip=%d",
			       dcpu.registers[0], dcpu.registers[1],
			       dcpu.registers[2], dcpu.registers[3],
			       dcpu.registers[4], dcpu.registers[5],
			       dcpu.registers[6], dcpu.registers[7],
			       dcpu.pc, dcpu.sp, dcpu.cycles, dcpu.skipping); */
			/* usleep(20000);*/
			if (dcpu.skipping) {
				cycle(&dcpu);
				continue;
			}
		}
	} else {
		fprintf(stderr, "%s: %s", except->desc, except->what);
		goto label;
	}
}
