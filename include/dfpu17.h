extern void dfpu17_cycle(struct hardware *hardware, u16 *dirty, struct dcpu *dcpu);
extern void dfpu17_interrupt(struct hardware *hardware, struct dcpu *dcpu);
extern struct device *make_dfpu17(struct dcpu *dcpu);
#define dfpu17_get(value, member) get_member_of(struct device_dfpu17, (value), member)
