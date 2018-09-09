struct dcpu;
struct hardware;
struct device {
	u32 id;
	u16 version;
	u32 manufacturer;
	void *data;
	void (*interrupt)(struct hardware *hardware, struct dcpu *dcpu);
	void (*cycle)(struct hardware *hardware, u16 *dirty, struct dcpu *dcpu);
};
#define DEVICE_INIT(i, ver, man) (struct device){.id=(i), .version=(ver),\
	.manufacturer=(man), .interrupt=&noop_interrupt, .cycle=&noop_cycle}
/**
 * represents a connection from a hardware device to a dcpu.
 * multiple dcpus may be connected to the same hardware device, in which case
 * there will be multiple 'struct hardware's but only a single 'struct device'.
 *
 * (currently just a device but it will be more in the future.)
 */
struct hardware {
	struct device *device;
};
enum dcpu_quirks {
	DCPU_QUIRKS_LEM1802_ALWAYS_ON = 1,
	DCPU_QUIRKS_LEM1802_USE_16BIT_COLOUR = 2,
};
struct dcpu {
	u16 registers[8];
	u16 ram[65536];
	u16 pc, sp, ex, ia;
	int skipping;
	int cycles;
	int queue_interrupts;
	u16 hw_count;
	struct hardware *hw;
	int quirks;
};
#define DCPU_INIT {.registers={0}, .ram={0}, .pc=0, .sp=0, .ex=0, .ia=0,\
	           .skipping=0, .cycles=0, .queue_interrupts=0,\
	           .hw_count=0, .hw=NULL, .quirks=0}
