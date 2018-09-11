#define _GNU_SOURCE
#include <math.h>
#include <tgmath.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "dcpu.h"
#include "exception.h"
#include "utils.h"
#include "dfpu17.h"

struct device_dfpu17 {
	u16 mode;
	u16 prec;
	u8  status;
	u8  error;
	u16 intrmsg;

	/**
	 * pointer/index into the dcpu's ram. this counts down,
	 * so we initialise it count-high.
	 */
	u8  loadstatus;
	u16 loadptr;
	u16 loadcount;

	u8  fcreg[4];
	u8  fcstack[4];
	u8  fcsp;
	u8  pc;

	union device_dfpu17_memory {
		struct device_dfpu17_memory_u16 {
			u16 registers[16];
			u16 data0[512];
			u16 data1[512];
		} word;
		struct device_dfpu17_memory_f16 {
			f16 registers[16];
			f16 data0[256]; /* unfortunate */
			f16 data1[256];
		} half;
		struct device_dfpu17_memory_f32 {
			f32 registers[8];
			f32 data0[256];
			f32 data1[256];
		} sngl;
		struct device_dfpu17_memory_f64 {
			f64 registers[4];
			f64 data0[128];
			f64 data1[128];
		} dble;
	} mem;

	u16 text[256];
};

enum dfpu17_command {
	SET_MODE,
	SET_PREC,
	GET_STATUS,
	LOAD_DATA,
	LOAD_TEXT,
	GET_DATA,
	EXECUTE,
	SWAP_BUFFERS,
	SET_INTERRUPT_MESSAGE
};

enum dfpu17_mode {
	MODE_OFF,
	MODE_INT,
	MODE_POLL
};

enum dfpu17_prec {
	PREC_SINGLE,
	PREC_DOUBLE,
	PREC_HALF
};

enum dfpu17_status {
	STATUS_OFF,
	STATUS_IDLE,
	STATUS_RUNNING,
	STATUS_WAITING
};

enum dfpu17_loadstatus {
	LOADSTATUS_NONE,
	LOADSTATUS_LOADING_TEXT,
	LOADSTATUS_LOADING_DATA,
	LOADSTATUS_STORING_DATA
};

enum dfpu17_error {
	ERROR_NONE,
	ERROR_FAIL,     /* an error set by the `fail' instruction */
	ERROR_INVALID,  /* invalid instruction */
	ERROR_FPEXCEPT  /* floating-point exception */
};

void dfpu17_enqueue_interrupt(struct dcpu *dcpu)
{
	/* cpu interrupts are not yet implemented */
	(void)dcpu;
}

/* this should probably work by swapping pointers or something instead.. */
void dfpu17_swap_buffers(struct device *device)
{
	u16 temporary[512];

	memcpy(temporary,
	       dfpu17_get(device, mem.word.data0),
	       512 * sizeof(u16));

	memcpy(dfpu17_get(device, mem.word.data0),
	       dfpu17_get(device, mem.word.data1),
	       512 * sizeof(u16));

	memcpy(dfpu17_get(device, mem.word.data1),
	       temporary,
	       512 * sizeof(u16));
}

void dfpu17_halt(struct device *device, struct dcpu *dcpu, int status, int error)
{
	dfpu17_get(device, status) = status;
	dfpu17_get(device, error) = error;

	if (dfpu17_get(device, mode) == MODE_INT) {
		dfpu17_swap_buffers(device);
		dfpu17_enqueue_interrupt(dcpu);
	}
}

void invalid_precision(u16 x) {
	(void)x;
	abort();
}

#define SWAP(x,y) do { __typeof(x) _tmp = x; x = y; y = _tmp; } while (0)
#define IREG  dfpu17_get(hw->device, fcreg)
#define ISTACK dfpu17_get(hw->device, fcstack)
#define ISP   dfpu17_get(hw->device, fcsp)
#define PREC  dfpu17_get(hw->device, prec)
#define PC    dfpu17_get(hw->device, pc)
#define COUNT dfpu17_get(hw->device, loadcount)
#define PTR   dfpu17_get(hw->device, loadptr)
#define TEXT  dfpu17_get(hw->device, text)
#define DATA  dfpu17_get(hw->device, mem.word.data1)
#define HREG  dfpu17_get(hw->device, mem.half.registers)
#define SREG  dfpu17_get(hw->device, mem.sngl.registers)
#define DREG  dfpu17_get(hw->device, mem.dble.registers)
#define HMEM  dfpu17_get(hw->device, mem.half.data0)
#define SMEM  dfpu17_get(hw->device, mem.sngl.data0)
#define DMEM  dfpu17_get(hw->device, mem.dble.data0)
#define BINOP(O,R,S) do {\
	if (PREC == PREC_HALF) {\
		f16 r = HREG[R];\
		f16 s = HREG[S];\
		(void)r; (void)s;\
		HREG[R] = O;\
	} else if (PREC == PREC_SINGLE) {\
		f32 r = SREG[R];\
		f32 s = SREG[S];\
		(void)r; (void)s;\
		SREG[R] = O;\
	} else {\
		f64 r = DREG[R];\
		f64 s = DREG[S];\
		(void)r; (void)s;\
		DREG[R] = O;\
	} } while (0)
#define UNARYOP(O,A) do {\
	if (PREC == PREC_HALF) {\
		f16 a = HREG[A];\
		(void)a;\
		HREG[A] = O;\
	} else if (PREC == PREC_SINGLE) {\
		f32 a = SREG[A];\
		(void)a;\
		SREG[A] = O;\
	} else {\
		f64 a = DREG[A];\
		(void)a;\
		DREG[A] = O;\
	} } while (0)
#define CMP(O,A,B) (!((PREC == PREC_HALF) ? (HREG[A] O HREG[B]) \
		    : (PREC == PREC_SINGLE) ? (SREG[A] O SREG[B]) \
		    : (DREG[A] O DREG[B])))


void dfpu17_execute_short(struct hardware *hw, struct dcpu *dcpu, u8 instruction)
{
	(void)hw;
	(void)dcpu;

	switch ((instruction >> 4) & 0x7) {
	case 0x0: { /* one-operand instructions */
#define A ((instruction >> 0) & 0x3)
		switch ((instruction >> 2) & 0x3) {
		case 0x0: /* zero @a */
			IREG[A] = 0;
			return;
		case 0x1: /* inc @a */
			IREG[A]++;
			return;
		case 0x2: /* dec @a */
			IREG[A]--;
			return;
		case 0x3: /* jmp @a */
			PC = IREG[A];
			return;
		}
#undef A
	}
#define A ((instruction >> 2) & 0x3)
#define B ((instruction >> 0) & 0x3)
	case 0x1: /* jc @a,@b */
		if (IREG[A] == 0)
			PC = IREG[B];
		return;
	case 0x2: /* set @a,@b */
		IREG[A] = IREG[B];
		return;
	case 0x3: /* swap @a,@b */
		SWAP(IREG[A], IREG[B]);
		return;
#undef B
#undef A
#define R ((instruction >> 2) & 0x3)
#define S ((instruction >> 0) & 0x3)
	case 0x4: /* mov %r,%s */
		BINOP(s, R, S);
		return;
	case 0x5: /* add %r,%s */
		BINOP(r + s, R, S);
		return;
	case 0x6: /* sub %r,%s */
		BINOP(r - s, R, S);
		return;
	case 0x7: /* mul %r,%s */
		BINOP(r * s, R, S);
		return;
#undef R
#undef S
	}
}

static void print_state(struct hardware *hw, u16 instruction)
{
	int i, j;
	
	fprintf(stderr, "float registers:\n");
	fprintf(stderr, "\t");
	for (i = 0; i < 8; i++) {
		fprintf(stderr, "%f ", SREG[i]);
	}
	fprintf(stderr, "\n");

	fprintf(stderr, "int registers:\n");
	fprintf(stderr, "\t");
	for (i = 0; i < 4; i++) {
		fprintf(stderr, "%d ", IREG[i]);
	}
	fprintf(stderr, "\n");

	fprintf(stderr, "int stack:\n");
	fprintf(stderr, "\t");
	for (i = 0; i < 4; i++) {
		fprintf(stderr, "%d ", ISTACK[i]);
	}
	fprintf(stderr, "\n");

	fprintf(stderr, "memory:\n");
	for (i = 0; i < 8; i++) {
		fprintf(stderr, "\t");
		for (j = 0; j < 16; j++) {
			fprintf(stderr, "%f ", SMEM[i * 16 + j]);
		}
		fprintf(stderr, "\n");
	} 

	fprintf(stderr, "status=%d, loadstatus=%d, error=%d\n",
		dfpu17_get(hw->device, status),
		dfpu17_get(hw->device, loadstatus),
		dfpu17_get(hw->device, error));
	fprintf(stderr, "instruction: 0x%04x\n", instruction);
}

void dfpu17_execute(struct hardware *hw, struct dcpu *dcpu)
{
	u16 instruction = dfpu17_get(hw->device, text)[dfpu17_get(hw->device, pc)++];

#if 0
	print_state(hw, instruction);
	if (!setjmp(except_buf)) {
#endif
		/*
		int i, j;
		fprintf(stderr, "FPU: 0x%04x\n", instruction);

		fprintf(stderr, "registers:\n");
		fprintf(stderr, "\t");
		for (i = 0; i < 8; i++) {
			fprintf(stderr, "%f ", SREG[i]);
		}
		fprintf(stderr, "\n");

		fprintf(stderr, "memory:\n");
		for (i = 0; i < 8; i++) {
			fprintf(stderr, "\t");
			for (j = 0; j < 16; j++) {
				fprintf(stderr, "%f ", SMEM[i * 16 + j]);
			}
			fprintf(stderr, "\n");
		} 
		*/

		if ((instruction & 0x8080) == 0x8080) {
			/* The MSB is executed first. TODO: explain why */
			dfpu17_execute_short(hw, dcpu, (instruction >> 8) & 0x7f);
			dfpu17_execute_short(hw, dcpu, (instruction >> 0) & 0x7f);
			return;
		}

		switch ((instruction >> 12) & 0xf) {
		case 0x0: /* nullary, @,@ and @ ops */
			switch ((instruction >> 11) & 0x1) {
			case 0x0:
				switch (instruction & 0x7ff) {
				case 0x0: /* noop */
					return;
				case 0x1: /* wait */
					dfpu17_get(hw->device, status) = STATUS_WAITING;
					return;
				case 0x2: /* halt */
					print_state(hw, instruction);
					dfpu17_halt(hw->device, dcpu, STATUS_IDLE, ERROR_NONE);
					return;
				case 0x3: /* fail */
					dfpu17_halt(hw->device, dcpu, STATUS_IDLE, ERROR_FAIL);
					return;
				case 0x7ff: /* debug */
					fprintf(stderr, "+===================+\n");
					fprintf(stderr, "| DEBUG INSTRUCTION |\n");
					fprintf(stderr, "+===================+\n");
					print_state(hw, instruction);
					getchar();
					return;
				default:
					throw("dfpu17opcode", "out of range");
				}
			case 0x1:
				switch ((instruction >> 8) & 0x7) {
					case 0x0:
#define A ((instruction >> 2) & 0x3)
#define B ((instruction >> 0) & 0x3)
						switch ((instruction >> 4) & 0xf) {
						case 0x0: /* jc @a,@b */
							if (IREG[A] == 0)
								PC = IREG[B];
							return;
						case 0x1: /* set @a,@b */
							IREG[A] = IREG[B];
							return;
						case 0x2: /* swap @a,@b */
							SWAP(IREG[A], IREG[B]);
							return;
						default: /* [unassigned] */
							throw("dfpu17opcode", "out of range");
						}
#undef B
#undef A
					case 0x1:
#define A ((instruction >> 0) & 0x3)
						switch ((instruction >> 2) & 0x3f) {
						case 0x0: /* zero @a */
							IREG[A] = 0;
							return;
						case 0x1: /* jmp @a */
							PC = IREG[A];
							return;
						case 0x2: /* inc @a */
							IREG[A]++;
							return;
						case 0x3: /* dec @a */
							IREG[A]--;
							return;
						case 0x4: /* push @a */
							ISP = (ISP - 1);
							ISP = (ISP % 4);
							ISTACK[ISP] = IREG[A];
							return;
						case 0x5: /* pop @a */
							IREG[A] = ISTACK[ISP];
							ISP = (ISP + 1) % 4;
							return;
						case 0x6: /* peek @a */
							IREG[A] = ISTACK[ISP];
							return;
						case 0x7: /* [unassigned] */
							throw("dfpu17opcode", "out of range");
						default:  /* [unassigned] */
							throw("dfpu17opcode", "out of range");
						}
#undef A
					case 0x2:
#define A ((instruction >> 0) & 0xf)
#define B ((instruction >> 4) & 0x3)
						switch ((instruction >> 6) & 0x3) {
						case 0x0: /* ld %a,@b */
							if (PREC == PREC_HALF)
								HREG[A] = HMEM[IREG[B]];
							else if (PREC == PREC_SINGLE)
								SREG[A] = SMEM[IREG[B]];
							else
								DREG[A] = DMEM[IREG[B]];
							return;
						case 0x1: /* st @b,%a */
							if (PREC == PREC_HALF)
								HMEM[IREG[B]] = HREG[A];
							else if (PREC == PREC_SINGLE)
								SMEM[IREG[B]] = SREG[A];
							else
								DMEM[IREG[B]] = DREG[A];
							return;
						case 0x2: /* [unassigned] */
							throw("dfpu17opcode", "out of range");
						case 0x3: /* [unassigned] */
							throw("dfpu17opcode", "out of range");
						}
#undef B
#undef A
					case 0x7: /* jmp $b */
#define B ((instruction >> 0) & 0xff)
						PC = B;
						return;
#undef B
					default: /* [unassigned] */
						throw("dfpu17opcode", "out of range");
				}
			}
		case 0x1: /* various @/$ ops */
#define A ((instruction >> 8) & 0x3)
#define B ((instruction >> 0) & 0xff)
			switch ((instruction >> 10) & 0x3) {
			case 0x0: /* loop @a,$b */
#if 0
				fprintf(stderr, "looping on @%d=0x%02x to 0x%02x\n",
					A, IREG[A], B);
#endif
				IREG[A]--;
				if (IREG[A] != 0)
					PC = B;
				return;
			case 0x1: /* jc @a,$b */
				if (IREG[A] == 0)
					PC = B;
				return;
			case 0x2: /* set @a,$b */
				IREG[A] = B;
				return;
			case 0x3: /* cmp @a,$b */
				if (IREG[A] > B)
					IREG[A] = 1;
				else if (IREG[A] < B)
					IREG[A] = -1;
				else
					IREG[A] = 0;
				return;
				throw("dfpu17opcode", "out of range");
			}
#undef B
#undef A
#define A ((instruction >> 8) & 0xf)
#define B ((instruction >> 0) & 0xff)
		case 0x2: /* ld %,$ */
			if (PREC == PREC_HALF)
				HREG[A] = HMEM[B];
			else if (PREC == PREC_SINGLE)
				SREG[A] = SMEM[B];
			else
				DREG[A] = DMEM[B];
			return;
		case 0x3: /* st %,$ */
			if (PREC == PREC_HALF)
				HMEM[B] = HREG[A];
			else if (PREC == PREC_SINGLE)
				SMEM[B] = SREG[A];
			else
				DMEM[B] = DREG[A];
			return;
#undef B
#undef A
		case 0x4: /* %,% and % ops */
			switch ((instruction >> 11) & 0x1) {
			case 0x0:
#define A ((instruction >> 0) & 0xf)
				switch ((instruction >> 8) & 0x7) {
				case 0x0:
					switch ((instruction >> 4) & 0xf) {
					case 0x0: /* sin %a */
						UNARYOP(sin(a), A);
						return;
					case 0x1: /* cos %a */
						UNARYOP(cos(a), A);
						return;
					case 0x2: /* tan %a */
						UNARYOP(tan(a), A);
						return;
					case 0x3: /* asin %a */
						UNARYOP(asin(a), A);
						return;
					case 0x4: /* acos %a */
						UNARYOP(acos(a), A);
						return;
					case 0x5: /* atan %a */
						UNARYOP(atan(a), A);
						return;
					case 0x6: /* sqrt %a */
						UNARYOP(sqrt(a), A);
						return;
					case 0x7: /* rnd %a */
						UNARYOP(round(a), A);
						return;
					case 0x8: /* log10 %a */
						UNARYOP(log10(a), A);
						return;
					case 0x9: /* log2 %a */
						UNARYOP(log2(a), A);
						return;
					case 0xa: /* log %a */
						UNARYOP(log(a), A);
						return;
					case 0xb: /* [unassigned] */
						throw("dfpu17opcode", "out of range");
					case 0xc: /* abs %a */
						UNARYOP(fabs(a), A);
						return;
					default:  /* [unassigned] */
						throw("dfpu17opcode", "out of range");
					}
				case 0x1:
					switch ((instruction >> 4) & 0xf) {
					case 0x0: /* ldz %a */
						UNARYOP(0.0, A);
						return;
					case 0x1: /* ld1 %a */
						UNARYOP(1.0, A);
						return;
					case 0x2: /* ldpi %a */
						UNARYOP(M_PI, A);
						return;
					case 0x3: /* lde %a */
						UNARYOP(M_E, A);
						return;
					case 0x4: /* ldsr2 %a */
						UNARYOP(sqrt(2.0), A);
						return;
					case 0x5: /* ldphi %a */
						UNARYOP(1.6180339887498948482, A);
						return;
					case 0x6: /* [unassigned] */
						throw("dfpu17opcode", "out of range");
					case 0x7: /* [unassigned] */
						throw("dfpu17opcode", "out of range");
					case 0x8: /* ldl2e %a */
						UNARYOP(log2(M_E), A);
						return;
					case 0x9: /* ldl2x %a */
						UNARYOP(log2(10.0), A);
						return;
					case 0xa: /* ldlg2 %a */
						UNARYOP(log10(2), A);
						return;
					case 0xb: /* ldln2 */
						UNARYOP(log(2), A);
						return;
					default:  /* [unassigned] */
						throw("dfpu17opcode", "out of range");
					}
				default: /* [unassigned] */
					throw("dfpu17opcode", "out of range");
				}
#undef A
			case 0x1:
#define R ((instruction >> 0) & 0xf)
#define S ((instruction >> 4) & 0xf)
				switch ((instruction >> 8) & 0x7) {
				case 0x0: /* mov %r,%s */
					BINOP(s, R, S);
					return;
				case 0x1: /* xchg %r,%s */
					if (PREC == PREC_HALF)
						SWAP(HREG[R], HREG[S]);
					else if (PREC == PREC_SINGLE)
						SWAP(SREG[R], SREG[S]);
					else
						SWAP(DREG[R], DREG[S]);
					return;
				case 0x2: /* add %r,%s */
					BINOP(r + s, R, S);
					return;
				case 0x3: /* mul %r,%s */
					BINOP(r * s, R, S);
					return;
				case 0x4: /* sub %r,%s */
					BINOP(r - s, R, S);
					return;
				case 0x5: /* rsub %r,%s */
					BINOP(s - r, R, S);
					return;
				case 0x6: /* div %r,%s */
					BINOP(r / s, R, S);
					return;
				case 0x7: /* rdiv %r,%s */
					BINOP(s / r, R, S);
					return;
				}
#undef S
#undef R
			}
		case 0x5: /* comparisons */
#define A ((instruction >> 0) & 0xf)
#define B ((instruction >> 4) & 0xf)
#define C ((instruction >> 8) & 0x3)
			switch ((instruction >> 10) & 0x3) {
			case 0x0: /* lt @c,%a,%b */
				IREG[C] = CMP(<, A, B);
				return;
			case 0x1: /* gt @c,%a,%b */
				IREG[C] = CMP(>, A, B);
				return;
			case 0x2: /* eq @c,%a,%b */
				IREG[C] = CMP(==, A, B);
				return;
			case 0x3: /* ne @c,%a,%b */
				IREG[C] = CMP(!=, A, B);
				return;
			}
#undef C
#undef B
#undef A
#define A ((instruction >> 0) & 0xf)
#define B ((instruction >> 4) & 0xf)
#define C ((instruction >> 8) & 0xf)
		case 0x6: /* atan %a,%y,%x */
			if (PREC == PREC_HALF)
				HREG[A] = atan2(HREG[B], HREG[C]);
			else if (PREC == PREC_SINGLE)
				SREG[A] = atan2(SREG[B], SREG[C]);
			else
				DREG[A] = atan2(DREG[B], DREG[C]);
			return;
		case 0x7: /* fma  %a,%b,%c*/
			if (PREC == PREC_HALF)
				HREG[A] = fma(HREG[A], HREG[B], HREG[C]);
			else if (PREC == PREC_SINGLE)
				SREG[A] = fma(SREG[A], SREG[B], SREG[C]);
			else
				DREG[A] = fma(DREG[A], DREG[B], DREG[C]);
			return;
#undef C
#undef B
#undef A
		}

#if 0
	} else {
		fprintf(stderr, "((instruction >> 12) & 0xf) == 0x%04x\n",
			(instruction >> 12) & 0xf);
		fprintf(stderr, "((instruction >> 11) & 0x1) == 0x%04x\n",
			(instruction >> 11) & 0x1);
		fprintf(stderr, "((instruction >> 8) & 0x7) == 0x%04x\n",
			(instruction >> 8) & 0x7);
		fprintf(stderr, "invalid instruction: 0x%04x @ 0x%04x\n", instruction, 0);
		fprintf(stderr, "what");
		abort();
	}
#endif
	/* all possible cases are covered above */
	abort();
}

void dfpu17_cycle(struct hardware *hw, u16 *dirty, struct dcpu *dcpu)
{
	/* the DFPU-17 is not memory-mapped and thus doesn't care about dirty */
	(void)dirty;

	if (dfpu17_get(hw->device, mode) == MODE_OFF)
		return;

#if 0
	fprintf(stderr, "FPU: status=%d error=%d loadstatus=%d count=%d loadptr=0x%04x\n",
			dfpu17_get(hw->device, status),
			dfpu17_get(hw->device, error),
			dfpu17_get(hw->device, loadstatus),
			COUNT, PTR);
#endif

	if (dfpu17_get(hw->device, status) == STATUS_RUNNING)
		dfpu17_execute(hw, dcpu);

	if (COUNT != 0) {
		switch (dfpu17_get(hw->device, loadstatus)) {
		case LOADSTATUS_NONE:
			COUNT = 0;
			break;
		case LOADSTATUS_LOADING_TEXT:
			TEXT[--COUNT] = dcpu->ram[--PTR];
			break;
		case LOADSTATUS_LOADING_DATA:
			DATA[--COUNT] = dcpu->ram[--PTR];
			break;
		case LOADSTATUS_STORING_DATA:
			dcpu->ram[--PTR] = DATA[--COUNT];
			break;
		}
		if (COUNT == 0)
			dfpu17_get(hw->device, loadstatus) = 0;
	}
#if 0
	fprintf(stderr, "status=%d, loadstatus=%d, error=%d\n",
		dfpu17_get(hw->device, status),
		dfpu17_get(hw->device, loadstatus),
		dfpu17_get(hw->device, error));
#endif
}

void dfpu17_interrupt(struct hardware *hw, struct dcpu *dcpu)
{
	switch (dcpu->registers[5]) {
	case SET_MODE:
		fprintf(stderr, "SET MODE TO %d\n", dcpu->registers[0]);
		if (dcpu->registers[0] == MODE_OFF) {
			dfpu17_get(hw->device, mode) = MODE_OFF;
			dfpu17_get(hw->device, status) = STATUS_OFF;
			dfpu17_get(hw->device, loadstatus) = LOADSTATUS_NONE;
		} else {
			dfpu17_get(hw->device, mode) = dcpu->registers[0];
			dfpu17_get(hw->device, status) = STATUS_IDLE;
			dfpu17_get(hw->device, loadstatus) = LOADSTATUS_NONE;
		}
		break;
	case SET_PREC:
		fprintf(stderr, "SET PRECISION TO %d\n", dcpu->registers[0]);
		dfpu17_get(hw->device, prec) = dcpu->registers[0];
		break;
	case GET_STATUS:
		/* fprintf(stderr, "GET STATUS\n"); */
		dcpu->registers[0] = (dfpu17_get(hw->device, loadstatus) << 8)
			           | (dfpu17_get(hw->device, status)     << 0);
		dcpu->registers[1] = dfpu17_get(hw->device, error);
		dcpu->registers[2] = 0;
		dcpu->registers[3] = 0;
		break;
	case LOAD_DATA:
		fprintf(stderr, "LOAD DATA @ 0x%04x\n", dcpu->registers[0]);
		dfpu17_get(hw->device, loadptr) = dcpu->registers[0] + 512;
		dfpu17_get(hw->device, loadcount) = 512;
		dfpu17_get(hw->device, loadstatus) = LOADSTATUS_LOADING_DATA;
		break;
	case LOAD_TEXT:
		fprintf(stderr, "LOAD TEXT @ 0x%04x\n", dcpu->registers[0]);
		dfpu17_get(hw->device, loadptr) = dcpu->registers[0] + 256;
		dfpu17_get(hw->device, loadcount) = 256;
		dfpu17_get(hw->device, loadstatus) = LOADSTATUS_LOADING_TEXT;
		break;
	case GET_DATA:
		fprintf(stderr, "GET DATA @ 0x%04x\n", dcpu->registers[0]);
		dfpu17_get(hw->device, loadptr) = dcpu->registers[0] + 512;
		dfpu17_get(hw->device, loadcount) = 512;
		dfpu17_get(hw->device, loadstatus) = LOADSTATUS_STORING_DATA;
		break;
	case EXECUTE:
		fprintf(stderr, "EXECUTE\n");
		if (dfpu17_get(hw->device, mode) == MODE_OFF) {
			break;
		} else if (dfpu17_get(hw->device, mode) == MODE_INT) {
			dfpu17_swap_buffers(hw->device);
		}
		dfpu17_get(hw->device, status) = STATUS_RUNNING;
		break;
	case SWAP_BUFFERS:
		fprintf(stderr, "SWAP BUFFERS\n");
		dfpu17_swap_buffers(hw->device);
		break;
	case SET_INTERRUPT_MESSAGE:
		fprintf(stderr, "SET INTERRUPT MESSAGE TO %d\n", dcpu->registers[0]);
		dfpu17_get(hw->device, intrmsg) = dcpu->registers[0];
		break;
	}
}

struct device *make_dfpu17(struct dcpu *dcpu)
{
	char *data = emalloc(sizeof(struct device) + sizeof(struct device_dfpu17));
	struct device *device = (struct device *)data;
	struct device_dfpu17 *dfpu17 = (struct device_dfpu17 *)(data + sizeof(struct device));

	struct device_dfpu17 d17 = {
		MODE_OFF,
		PREC_SINGLE,
		STATUS_OFF,
		ERROR_NONE,
		0x0000,
		LOADSTATUS_NONE,
		0x0000,
		0x0000,
		{0x00, 0x00, 0x00, 0x00},
		{0x00, 0x00, 0x00, 0x00},
		0x00,
		0x00,
		{0},
		{0},
	};

	struct device d = {
		0x1de171f3,
		0x0001,
		0x5307537,
		NULL,
		&dfpu17_interrupt,
		&dfpu17_cycle,
	};

	d.data = dfpu17;

	*dfpu17 = d17;
	*device = d;

	/* don't need to use this */
	(void)dcpu;

	return device;
}
