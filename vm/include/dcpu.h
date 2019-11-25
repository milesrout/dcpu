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
	DCPU_QUIRKS_LEM1802_USE_16BIT_COLOUR = 2
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
extern const struct dcpu dcpu_init;
extern const struct device device_init;
#define DCPU_INIT dcpu_init
