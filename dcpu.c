#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "types.h"
#include "exception.h"

struct dcpu;

struct device {
	u32 id;
	u16 version;
	u32 manufacturer;

	void (*interrupt)(void *device, struct dcpu *dcpu);
};

struct hardware {
	struct hardware *next;
	struct device   *device;
};

struct dcpu {
	u16 registers[8];
	u16 ram[65536];
	u16 pc, sp, ex, ia;
	int cycles;
	bool queue_interrupts;
	struct hardware *hw;
};

struct device *nth_device(struct dcpu *dcpu, u16 n)
{
	struct hardware *hw = dcpu->hw;
	for (int i = 0; i < n; i++) {
		if (hw) {
			hw = hw->next;
		} else {
			return NULL;
		}
	}
	return hw->device;
}

#define DCPU_INIT {.registers={0}, .ram={0}, .pc=0, .sp=0, .ex=0, .ia=0,\
	           .cycles=0, .queue_interrupts=true, .hw=NULL}

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

static u16 decode_a(struct dcpu *dcpu, u16 a)
{
	if (a >= 0x40) throw("decode_a", "too large");
	if (a == 0x18) return dcpu->ram[dcpu->sp++];
	if (a < 0x20) return *decode_b(dcpu, a);

	// 0x20-0x3f | literal value 0xffff-0x1e (-1..30) (literal) (only for a)
	return a - 0x21;
}

static void interrupt(struct dcpu *dcpu, u16 message)
{
	throw("interrupt", "not yet implemented");
}

static void cycle(struct dcpu *dcpu)
{
	u16 instruction = dcpu->ram[dcpu->pc++];
	u16 opcode = instruction & 0b11111;
	u16 enc_b = (instruction & 0b1111100000) >> 5;
	u16 enc_a = (instruction & 0b1111110000000000) >> 10;

	printf("a=0x%02x b=0x%02x o=0x%02x\n", enc_a, enc_b, opcode);

	if (opcode == 0x00) switch (enc_b) {
		u16   a = decode_a(dcpu, enc_a);
		u16 *pa = decode_b(dcpu, enc_a);
		case 0x01:
			dcpu->ram[--dcpu->sp] = dcpu->pc;
			dcpu->pc = a;
			break;
		case 0x08:
			interrupt(dcpu, a);
			break;
		case 0x09:
			*decode_b(dcpu, enc_a) = dcpu->ia;
			break;
		case 0x0a:
			dcpu->ia = a;
			break;
		case 0x0b:
			dcpu->queue_interrupts = false;
			dcpu->registers[0] = dcpu->ram[dcpu->sp++];
			dcpu->pc = dcpu->ram[dcpu->sp++];
			break;
		case 0x0c:
			dcpu->queue_interrupts = (a != 0);
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
			break;
		}
		case 0x11:
		{
			struct device *device = nth_device(dcpu, a);
			if (device != NULL) {
				dcpu->registers[0] = device->id & 0x0f;
				dcpu->registers[1] = (device->id & 0xf0) >> 16;
				dcpu->registers[2] = device->version;
				dcpu->registers[3] = device->manufacturer & 0x0f;
				dcpu->registers[4] = (device->manufacturer & 0xf0) >> 16;
			}
			break;
		}
		case 0x12:
		{
			struct device *device = nth_device(dcpu, a);
			if (device != NULL) {
				device->interrupt(device, dcpu);
			}
			break;
		}
		default: throw("opcode", "out of range");
	} else {
		u16  a = decode_a(dcpu, enc_a);
		u16 *b = decode_b(dcpu, enc_b);
		if (opcode == 0x01) {
			*b = a;
		} else if (opcode == 0x02) {
			u32 c    = (u32)a + (u32)*b;
			dcpu->ex = c >> 16;
			*b       = c;
		} else if (opcode == 0x03) {
			u32 c    = (u32)*b - (u32)a;
			dcpu->ex = c >> 16;
			*b       = c;
		} else if (opcode == 0x04) {
			u32 c    = (u32)*b * (u32)a;
			dcpu->ex = c >> 16;
			*b       = c;
		} else if (opcode == 0x05) {
			s32 c    = (s32)(s16)*b * (s32)(s16)a;
			dcpu->ex = (u32)c >> 16;
			*b       = (u32)c;
		} else if (opcode == 0x06) {
			if (a == 0) {
				dcpu->ex = 0;
				*b       = 0;
			} else {
				u32 c = (u32)*b / (u32)a;
				dcpu->ex = c >> 16;
				*b       = c;
			}
		} else if (opcode == 0x07) {
			if (a == 0) {
				dcpu->ex = 0;
				*b       = 0;
			} else {
				s32 c = (s32)(s16)*b / (s32)(s16)a;
				dcpu->ex = (u32)c >> 16;
				*b       = (u32)c;
			}
		} else if (opcode == 0x08) {
			if (a == 0)
				*b = 0;
			else
				*b = *b % a;
		} else if (opcode == 0x09) {
			if (a == 0) {
				*b = 0;
			} else {
				u16 c = *b % a;
				*b = (s16)*b < 0 ? -(s16)c : c;
			}
		} else if (opcode == 0x0a) {
			*b = *b & a;
		} else if (opcode == 0x0b) {
			*b = *b | a;
		} else if (opcode == 0x0c) {
			*b = *b ^ a;
		} else if (opcode == 0x0d) {

		} else if (opcode == 0x0e) {

		} else if (opcode == 0x0f) {

		}
	}
}

int main()
{
	struct dcpu dcpu = DCPU_INIT;
	dcpu.ram[0] = 0x7c01;
	cycle(&dcpu);
}
