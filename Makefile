Q ?= @
CC = arm-none-eabi-gcc
NWLINK = nwlink

SRCS = src/main.c src/display.c src/keyboard.c src/sound.c src/storage.c src/timing.c src/heap.c \
       src/src/bus/gwenesis_bus.c \
       src/src/cpus/M68K/m68kcpu.c \
       src/src/cpus/Z80/Z80.c \
       src/src/io/gwenesis_io.c \
       src/src/savestate/gwenesis_savestate.c \
       src/src/vdp/gwenesis_vdp_gfx.c \
       src/src/vdp/gwenesis_vdp_mem.c \
       src/src/sound/z80inst.c \
       src/src/sound/gwenesis_sn76489.c \
       src/src/sound/ym2612.c

OBJS = $(SRCS:.c=.o)

CFLAGS = -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard
CFLAGS += -O3 -ffast-math -std=c99 -Wall
CFLAGS += -I. -Isrc -Isrc/src -Isrc/src/bus -Isrc/src/cpus/M68K -Isrc/src/cpus/Z80 -Isrc/src/io -Isrc/src/vdp -Isrc/src/savestate -Isrc/src/sound
CFLAGS += $(shell $(NWLINK) eadk-cflags-device)

# LTO + dead-code stripping - NOTE: deliberately NOT using -fwhole-program /
# -fvisibility=internal here, unlike NumWorks' own single-file sample
# Makefile. Those two are tuned for a build with exactly one translation
# unit; this project has many (gwenesis core split across bus/cpus/io/vdp/
# savestate), and -fwhole-program in particular can misbehave - or just be
# invalid - across multiple separately-compiled .c files. Plain -flto still
# gets you cross-file inlining/optimization at link time without that risk.
CFLAGS += -flto -fno-fat-lto-objects
CFLAGS += -fdata-sections -ffunction-sections

LDFLAGS = -Wl,--relocatable
LDFLAGS += -nostartfiles
LDFLAGS += --specs=nano.specs --specs=nosys.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections
LDFLAGS += -Wl,--defsym=__exidx_start=0 -Wl,--defsym=__exidx_end=0
LDFLAGS += -flinker-output=nolto-rel

# Must come AFTER the object files on the link line, not in LDFLAGS - the
# linker only pulls symbols out of a .a when something already on the
# command line needs them, so a library listed before the objects that
# reference it is silently skipped.
LDLIBS = -lm

.PHONY: build
build: gwenesis.nwa

.PHONY: run
run: gwenesis.nwa
	@echo "INSTALL $<"
	$(Q) $(NWLINK) install-nwa $<

gwenesis.nwa: $(OBJS) icon.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

%.o: %.c
	@echo "CC      $<"
	$(Q) $(CC) $(CFLAGS) -c $< -o $@

# NOTE: adjust this path if your icon.png lives somewhere other than the
# project root - your old broken recipe referenced a bare "icon.png", so
# that's the assumption here.
icon.o: icon.png
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

.PHONY: clean
clean:
	@echo "CLEAN"
	$(Q) rm -f $(OBJS) icon.o gwenesis.nwa