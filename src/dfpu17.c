#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "dcpu.h"
#include "utils.h"
#include "dfpu17.h"

struct device_dfpu17 {
	u16 mode;
	u16 prec;
	u16 status;
	u16 error;
	u16 intrmsg;

	/* pointer/index into the dcpu's ram */
	u16 loadstatus;
	u16 loadptr;
	u16 loadcount;

	u8  fcreg[4];

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
};

void dfpu17_cycle(struct hardware *hardware, u16 *dirty, struct dcpu *dcpu)
{
	(void)hardware;
	(void)dirty;
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
		dfpu17_get(hw->device, loadptr) = dcpu->registers[0];
		dfpu17_get(hw->device, loadcount) = 512;
		dfpu17_get(hw->device, loadstatus) = LOADSTATUS_LOADING_DATA;
		break;
	case LOAD_TEXT:
		dfpu17_get(hw->device, loadptr) = dcpu->registers[0];
		dfpu17_get(hw->device, loadcount) = 256;
		dfpu17_get(hw->device, loadstatus) = LOADSTATUS_LOADING_TEXT;
		break;
	case GET_DATA:
		dfpu17_get(hw->device, loadptr) = dcpu->registers[0];
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
