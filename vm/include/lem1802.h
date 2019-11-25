extern void lem1802_cycle(struct hardware *hardware, u16 *dirty, struct dcpu *dcpu);
extern void lem1802_interrupt(struct hardware *hardware, struct dcpu *dcpu);
extern struct device *make_lem1802(struct dcpu *dcpu);
