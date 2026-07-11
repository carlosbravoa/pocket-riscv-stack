# Shared game build rules — include from a game directory's Makefile:
#
#   GAME_SRCS = main.c sprites.c
#   include ../game.mk          # (path to this file)
#
# Produces game.bin: a flat binary linked at the DRAM game base, padded so the
# APF EOF-wedge never bites (the loader pulls size-2 bytes). Drop it anywhere on
# the Pocket SD (e.g. Assets/riscv_stack/common/) and pick it in the Game slot.
#
# v1 NOTE: builds against the SoC build tree in this repo (BUILD_DIR); a
# relocatable standalone SDK export is future work.
#
# SPDX-License-Identifier: BSD-2-Clause

SDK_DIR   := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
BUILD_DIR ?= $(SDK_DIR)/../soc/build/pocket

include $(BUILD_DIR)/software/include/generated/variables.mak
include $(SOC_DIRECTORY)/software/common.mak

# PORTLIB: opt-in SDK modules for ports (e.g. PORTLIB = pakfs sdl_lite)
OBJECTS  = crt0_game.o gamelib.o $(GAME_SRCS:.c=.o) $(PORTLIB:%=%.o) hal.o
CFLAGS  += -I$(SDK_DIR)/../soc/hal -I$(SDK_DIR)

# gamelib defines memcpy/memset/memmove: stop the compiler from recognizing
# their loops and emitting calls to themselves (infinite recursion).
gamelib.o: CFLAGS += -fno-builtin -fno-tree-loop-distribute-patterns

GAME ?= game

all: $(GAME).bin

$(GAME).bin: $(GAME).elf
	$(OBJCOPY) -O binary $< $@
	@# pad: the loader never pulls the last 2 bytes of a file (APF EOF wedge)
	@printf '\0\0\0\0' >> $@
	@chmod -x $@ 2>/dev/null || true
	@ls -l $@

LIBFILES = $(foreach lib,$(LIBS),$(BUILD_DIR)/software/$(lib)/$(lib).a)

# Games link a FILTERED libc: picolibc-minimal's memcpy/memset/memmove are
# byte-per-iteration loops (~13 cycles/byte to DRAM) and gamelib overrides
# them with word-wide versions; strip libc's copies so --whole-archive
# doesn't collide.
# (common.mak's AR/OBJCOPY carry make's @ silencer — unusable in $(shell))
GAME_AR := $(TARGET_PREFIX)ar

libc_game.a: $(BUILD_DIR)/software/libc/libc.a
	cp $< $@
	$(GAME_AR) d $@ $(shell $(GAME_AR) t $(BUILD_DIR)/software/libc/libc.a | grep -E 'memcpy|memset|memmove')
	$(GAME_AR) t $@ | grep -qE 'memcpy|memset|memmove' && { echo "libc_game.a: strip failed"; exit 1; } || true

$(GAME).elf: $(OBJECTS) $(SDK_DIR)/game.ld $(LIBFILES) libc_game.a
	$(CC) $(LDFLAGS) -T $(SDK_DIR)/game.ld -N -o $@ \
		$(OBJECTS) \
		$(PACKAGES:%=-L$(BUILD_DIR)/software/%) \
		-Wl,--whole-archive \
		-Wl,--start-group \
		$(filter-out -lc,$(LIBS:lib%=-l%)) libc_game.a \
		-Wl,--end-group \
		-Wl,--no-whole-archive \
		-Wl,--gc-sections \
		-Wl,-Map,$@.map
	@chmod -x $@ 2>/dev/null || true

-include $(OBJECTS:.o=.d)

VPATH = $(SDK_DIR):$(SDK_DIR)/../soc/hal

%.o: %.c
	$(compile)

%.o: %.S
	$(assemble)

clean:
	$(RM) $(OBJECTS) $(OBJECTS:.o=.d) $(GAME).elf $(GAME).bin $(GAME).elf.map

.PHONY: all clean
