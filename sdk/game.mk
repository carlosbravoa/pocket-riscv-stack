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

OBJECTS  = crt0_game.o $(GAME_SRCS:.c=.o) hal.o
CFLAGS  += -I$(SDK_DIR)/../soc/hal

GAME ?= game

all: $(GAME).bin

$(GAME).bin: $(GAME).elf
	$(OBJCOPY) -O binary $< $@
	@# pad: the loader never pulls the last 2 bytes of a file (APF EOF wedge)
	@printf '\0\0\0\0' >> $@
	@chmod -x $@ 2>/dev/null || true
	@ls -l $@

LIBFILES = $(foreach lib,$(LIBS),$(BUILD_DIR)/software/$(lib)/$(lib).a)

$(GAME).elf: $(OBJECTS) $(SDK_DIR)/game.ld $(LIBFILES)
	$(CC) $(LDFLAGS) -T $(SDK_DIR)/game.ld -N -o $@ \
		$(OBJECTS) \
		$(PACKAGES:%=-L$(BUILD_DIR)/software/%) \
		-Wl,--whole-archive \
		-Wl,--start-group \
		$(LIBS:lib%=-l%) \
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
