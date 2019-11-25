ifeq ($(BUILD),release)
	CFLAGS += -O3 -s -DNDEBUG
else
	CFLAGS += -O0 -g
endif

TARGET    := a.out

PC_DEPS   := sdl2
PC_CFLAGS := $(shell pkg-config --cflags $(PC_DEPS))
PC_LIBS   := $(shell pkg-config --libs $(PC_DEPS))

EX_SRCS   := $(shell find examples -name *.dasm16)
EX_BINS   := $(EX_SRCS:.dasm16=.bin)
EX_HEXS   := $(EX_BINS:.bin=.hex)


SRCS      := $(shell find src -name *.c)
OBJS      := $(SRCS:%=build/%.o)
DEPS      := $(OBJS:%.o=%.d)

INCS      := $(addprefix -I,$(shell find ./include -type d))

CFLAGS    += $(PC_CFLAGS) $(INCS) -MMD -MP -pedantic -pedantic-errors -std=c89
LDLIBS    += $(PC_LIBS) -lm

build/$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

build/%.c.o: %.c
	mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $< -o $@
	@$(RM) *.d

tags:
	gcc -M $(INCS) $(PC_CFLAGS) $(SRCS) | sed -e 's/[\ ]/\n/g' | \
		sed -e '/^$$/d' -e '/\.o:[ \t]*$$/d' | \
		ctags -L - $(CTAGS_FLAGS)

%.bin: %.dasm16
	dtasm --binary $< -o $@

%.hex: %.bin
	python3 utils.py $< > $@

.PHONY: clean syntastic
clean:
	rm -f build/$(TARGET) $(OBJS) $(DEPS) $(EX_BINS)

syntastic:
	echo $(CFLAGS) | tr ' ' '\n' > .syntastic_c_config

release:
	-$(MAKE) "BUILD=release"

-include $(DEPS)
