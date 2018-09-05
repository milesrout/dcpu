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
	u16 status;
	u16 error;
	u16 intrmsg;

	/**
	 * pointer/index into the dcpu's ram. this counts down,
	 * so we initialise it count-high.
	 */
	u16 loadstatus;
	u16 loadptr;
	u16 loadcount;

	u8  fcreg[4];
	u8  pc;

	union {
		struct {
			u16 registers[16];
			u16 data0[512];
			u16 data1[512];
		} word;
		struct {
			f16 registers[16];
			f16 data0[512];
			f16 data1[512];
		} half;
		struct {
			f32 registers[8];
			f32 data0[256];
			f32 data1[256];
		} sngl;
		struct {
			f64 registers[4];
			f64 data0[128];
			f64 data1[128];
		} dble;
	};

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
	SET_INTERRUPT_MESSAGE,
};

enum dfpu17_mode {
	MODE_OFF,
	MODE_INT,
	MODE_POLL,
};

enum dfpu17_prec {
	PREC_SINGLE,
	PREC_DOUBLE,
	PREC_HALF,
};

enum dfpu17_status {
	STATUS_OFF,
	STATUS_IDLE,
	STATUS_RUNNING,
	STATUS_WAITING,
};

enum dfpu17_loadstatus {
	LOADSTATUS_NONE,
	LOADSTATUS_LOADING_TEXT,
	LOADSTATUS_LOADING_DATA,
	LOADSTATUS_STORING_DATA,
};

enum dfpu17_error {
	ERROR_NONE,
	ERROR_FAIL,
};

void dfpu17_enqueue_interrupt(struct dcpu *dcpu)
{
	/* cpu interrupts are not yet implemented */
	(void)dcpu;
}

void dfpu17_swap_buffers(struct device *device)
{
	u16 temporary[512];

	memcpy(temporary,
	       dfpu17_get(device, word.data0),
	       512 * sizeof(u16));

	memcpy(dfpu17_get(device, word.data0),
	       dfpu17_get(device, word.data1),
	       512 * sizeof(u16));

	memcpy(dfpu17_get(device, word.data1),
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

#define SWAP(x,y) ({ __typeof(x) _tmp = x; x = y; y = _tmp; })
#define IREG  dfpu17_get(hw->device, fcreg)
#define PREC  dfpu17_get(hw->device, prec)
#define PC    dfpu17_get(hw->device, pc)
#define COUNT dfpu17_get(hw->device, loadcount)
#define PTR   dfpu17_get(hw->device, loadptr)
#define TEXT  dfpu17_get(hw->device, text)
#define DATA  dfpu17_get(hw->device, word.data1)
#define SREG  dfpu17_get(hw->device, sngl).registers
#define HREG  dfpu17_get(hw->device, half).registers
#define DREG  dfpu17_get(hw->device, dble).registers
#define SMEM  dfpu17_get(hw->device, sngl).data0
#define HMEM  dfpu17_get(hw->device, half).data0
#define DMEM  dfpu17_get(hw->device, dble).data0

void dfpu17_execute_short(struct hardware *hw, struct dcpu *dcpu, u8 instruction)
{
#define BINOP(O,R,S) do {\
	if (PREC == PREC_HALF) {\
		f16 r = HREG[R]; (void)r;\
		f16 s = HREG[S]; (void)s;\
		O;\
	} else if (PREC == PREC_SINGLE) {\
		f32 r = SREG[R]; (void)r;\
		f32 s = SREG[S]; (void)s;\
		O;\
	} else {\
		f64 r = DREG[R]; (void)r;\
		f64 s = DREG[S]; (void)s;\
		O;\
	} } while (0)

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
		BINOP(r = s, R, S);
		return;
	case 0x5: /* add %r,%s */
		BINOP(r = r + s, R, S);
		return;
	case 0x6: /* sub %r,%s */
		BINOP(r = r - s, R, S);
		return;
	case 0x7: /* mul %r,%s */
		BINOP(r = r * s, R, S);
		return;
#undef R
#undef S
	}
}

void dfpu17_execute(struct hardware *hw, struct dcpu *dcpu)
{
	u16 instruction = dfpu17_get(hw->device, text)[dfpu17_get(hw->device, pc)++];

	(void)dcpu;

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
				dfpu17_halt(hw->device, dcpu, STATUS_IDLE, ERROR_NONE);
				return;
			case 0x3: /* fail */
				dfpu17_halt(hw->device, dcpu, STATUS_IDLE, ERROR_FAIL);
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
					default: /* [unassigned] */
						throw("dfpu17opcode", "out of range");
					}
#undef A
				case 0x2:
#define A ((instruction >> 0) & 0xf)
#define B ((instruction >> 4) & 0x3)
					switch ((instruction >> 6) & 0x3) {
					case 0x0: /* ld %a,@b */
						if (PREC == PREC_HALF)
							HREG[A] = HMEM[B];
						else if (PREC == PREC_SINGLE)
							SREG[A] = SMEM[B];
						else
							DREG[A] = DMEM[B];
						return;
					case 0x1: /* st @b,%a */
						if (PREC == PREC_HALF)
							HMEM[B] = HREG[A];
						else if (PREC == PREC_SINGLE)
							SMEM[B] = SREG[A];
						else
							DMEM[B] = DREG[A];
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
					PC = TEXT[PC + 1];
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
			return;
		case 0x1: /* jc @a,$b */
			return;
		case 0x2: /* set @a,$b */
			return;
		case 0x3: /* [unassigned] */
			return;
		}
#undef B
#undef A
	case 0x2: /* ld %,$ */
		return;
	case 0x3: /* st %,$ */
		return;
	case 0x4: /* %,% and % ops */
		switch ((instruction >> 11) & 0x1) {
		case 0x0:
			switch ((instruction >> 8) & 0x7) {
			case 0x0:
				switch ((instruction >> 4) & 0xf) {
				case 0x0: /* sin %a */
					return;
				case 0x1: /* cos %a */
					return;
				case 0x2: /* tan %a */
					return;
				case 0x3: /* asin %a */
					return;
				case 0x4: /* acos %a */
					return;
				case 0x5: /* atan %a */
					return;
				case 0x6: /* sqrt %a */
					return;
				case 0x7: /* rnd %a */
					return;
				case 0x8: /* log %a */
					return;
				case 0x9: /* log2 %a */
					return;
				case 0xa: /* ln %a */
					return;
				case 0xb: /* [unassigned] */
					throw("dfpu17opcode", "out of range");
				case 0xc: /* abs %a */
					return;
				default:  /* [unassigned] */
					throw("dfpu17opcode", "out of range");
				}
			case 0x1:
				switch ((instruction >> 4) & 0xf) {
				case 0x0: /* ldz %a */
					return;
				case 0x1: /* ld1 %a */
					return;
				case 0x2: /* ldpi %a */
					return;
				case 0x3: /* lde %a */
					return;
				case 0x4: /* ldsr2 %a */
					return;
				case 0x5: /* ldphi %a */
					return;
				case 0x6: /* [unassigned] */
					return;
				case 0x7: /* [unassigned] */
					return;
				case 0x8: /* ldl2e %a */
					return;
				case 0x9: /* ldl2t %a */
					return;
				case 0xa: /* ldlg2 %a */
					return;
				case 0xb: /* ldln2 */
					throw("dfpu17opcode", "out of range");
				default:  /* [unassigned] */
					throw("dfpu17opcode", "out of range");
				}
			default: /* [unassigned] */
				throw("dfpu17opcode", "out of range");
			}
		case 0x1:
			switch ((instruction >> 8) & 0x7) {
			case 0x0: /* mov %a,%b */
				return;
			case 0x1: /* xchg %a,%b */
				return;
			case 0x2: /* add %a,%b */
				return;
			case 0x3: /* mul %a,%b */
				return;
			case 0x4: /* sub %a,%b */
				return;
			case 0x5: /* rsub %a,%b */
				return;
			case 0x6: /* div %a,%b */
				return;
			case 0x7: /* rdiv %a,%b */
				return;
			}
		}
	case 0x5: /* comparisons */
		switch ((instruction >> 10) & 0x3) {
		case 0x0: /* lt @c,%a,%b */
			return;
		case 0x1: /* gt @c,%a,%b */
			return;
		case 0x2: /* eq @c,%a,%b */
			return;
		case 0x3: /* ne @c,%a,%b */
			return;
		}
	case 0x6: /* atan %a,%y,%x */
		return;
	case 0x7: /* fma  %a,%b,%c*/
		return;
	}

	/* all possible cases are covered above */
	abort();
}

void dfpu17_cycle(struct hardware *hw, u16 *dirty, struct dcpu *dcpu)
{

	/* the DFPU-17 is not memory-mapped and thus doesn't care about dirty */
	(void)dirty;

	if (dfpu17_get(hw->device, mode) == MODE_OFF)
		return;

	if (dfpu17_get(hw->device, status) == STATUS_RUNNING)
		dfpu17_execute(hw, dcpu);

	if (COUNT != 0) {
		switch (dfpu17_get(hw->device, loadstatus)) {
		case LOADSTATUS_NONE:
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
	}

#undef DATA
#undef TEXT
#undef PTR
#undef COUNT
}

void dfpu17_interrupt(struct hardware *hw, struct dcpu *dcpu)
{
	switch (dcpu->registers[5]) {
	case SET_MODE:
		dfpu17_get(hw->device, mode) = dcpu->registers[0];
		break;
	case SET_PREC:
		dfpu17_get(hw->device, prec) = dcpu->registers[0];
		break;
	case GET_STATUS:
		dcpu->registers[0] = dfpu17_get(hw->device, status);
		dcpu->registers[1] = dfpu17_get(hw->device, error);
		dcpu->registers[2] = 0;
		dcpu->registers[3] = 0;
		break;
	case LOAD_DATA:
		dfpu17_get(hw->device, loadptr) = dcpu->registers[0] + 512;
		dfpu17_get(hw->device, loadcount) = 512;
		dfpu17_get(hw->device, loadstatus) = LOADSTATUS_LOADING_DATA;
		break;
	case LOAD_TEXT:
		dfpu17_get(hw->device, loadptr) = dcpu->registers[0] + 256;
		dfpu17_get(hw->device, loadcount) = 256;
		dfpu17_get(hw->device, loadstatus) = LOADSTATUS_LOADING_TEXT;
		break;
	case GET_DATA:
		dfpu17_get(hw->device, loadptr) = dcpu->registers[0] + 512;
		dfpu17_get(hw->device, loadcount) = 512;
		dfpu17_get(hw->device, loadstatus) = LOADSTATUS_STORING_DATA;
		break;
	case EXECUTE:
		if (dfpu17_get(hw->device, mode) == MODE_OFF) {
			break;
		} else if (dfpu17_get(hw->device, mode) == MODE_INT) {
			dfpu17_swap_buffers(hw->device);
		}
		dfpu17_get(hw->device, status) = STATUS_RUNNING;
		break;
	case SWAP_BUFFERS:
		dfpu17_swap_buffers(hw->device);
		break;
	case SET_INTERRUPT_MESSAGE:
		dfpu17_get(hw->device, intrmsg) = dcpu->registers[0];
		break;
	}
}

struct device *make_dfpu17(struct dcpu *dcpu)
{
	char *data = emalloc(sizeof(struct device) + sizeof(struct device_dfpu17));
	struct device *device = (struct device *)data;
	struct device_dfpu17 *dfpu17 = (struct device_dfpu17 *)(data + sizeof(struct device));

	/* don't need to use this */
	(void)dcpu;

	*dfpu17 = (struct device_dfpu17){
		.mode=MODE_OFF,
		.prec=PREC_SINGLE,
		.status=STATUS_OFF,
		.error=ERROR_NONE,
		.intrmsg=0x0000,
		.loadstatus=LOADSTATUS_NONE,
		.loadptr=0x0000,
		.loadcount=0x0000,
		.fcreg={0x00, 0x00, 0x00, 0x00},
		.word={.registers={0}, .data0={0}, .data1={0}},
		.text={0},
	};

	*device = (struct device){
		.id=0x1de171f3,
		.version=0x0001,
		.manufacturer=0x5307537,
		.interrupt=&dfpu17_interrupt,
		.cycle=&dfpu17_cycle,
		.data=dfpu17,
	};

	return device;
}
