struct dcpu;
struct device {
	u32 id;
	u16 version;
	u32 manufacturer;
	void *data;
	void (*interrupt)(struct device *device, struct dcpu *dcpu);
	void (*cycle)(struct device *device, struct dcpu *dcpu);
};
#define DEVICE_INIT(i, ver, man) (struct device){.id=(i), .version=(ver), .manufacturer=(man), .interrupt=&getchar_interrupt, .cycle=&noop_cycle}
struct hardware {
	struct hardware *next;
	struct device   *device;
};
struct dcpu {
	u16 registers[8];
	u16 ram[65536];
	u16 pc, sp, ex, ia;
	int skipping;
	int cycles;
	int queue_interrupts;
	struct hardware *hw;
};
#define DCPU_INIT {.registers={0}, .ram={0}, .pc=0, .sp=0, .ex=0, .ia=0,\
	           .cycles=0, .queue_interrupts=true, .hw=NULL}
extern struct device *nth_device(struct dcpu *dcpu, u16 n);
