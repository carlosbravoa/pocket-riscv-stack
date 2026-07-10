# Stage-1 firmware build. Links against the SoC's generated headers + LiteX
# libbase/libc. BUILD_DIR must point at the SoC build directory that contains
# software/include/generated (e.g. ../build/pocket or ../build/sim).
#
# SPDX-License-Identifier: BSD-2-Clause

BUILD_DIR ?= ../build/pocket

include $(BUILD_DIR)/software/include/generated/variables.mak
include $(SOC_DIRECTORY)/software/common.mak

OBJECTS = crt0.o main.o hal.o
CFLAGS += -I$(CURDIR)/../hal

all: firmware.bin

# Convenience: emit a readelf-friendly disassembly next to the elf.
firmware.bin: firmware.elf
	$(OBJCOPY) -O binary $< $@
ifneq ($(OS),Windows_NT)
	chmod -x $@
endif

# Relink when the linker script or any LiteX static library changes (a liblitedram
# fix or SoC software rebuild must not leave a stale ELF linked against old libs).
LIBFILES = $(foreach lib,$(LIBS),$(BUILD_DIR)/software/$(lib)/$(lib).a)

firmware.elf: $(OBJECTS) linker.ld $(LIBFILES)
	$(CC) $(LDFLAGS) -T linker.ld -N -o $@ \
		$(OBJECTS) \
		$(PACKAGES:%=-L$(BUILD_DIR)/software/%) \
		-Wl,--whole-archive \
		-Wl,--start-group \
		$(LIBS:lib%=-l%) \
		-Wl,--end-group \
		-Wl,--no-whole-archive \
		-Wl,--gc-sections \
		-Wl,-Map,$@.map
ifneq ($(OS),Windows_NT)
	chmod -x $@
endif

-include $(OBJECTS:.o=.d)

VPATH = $(BIOS_DIRECTORY):$(BIOS_DIRECTORY)/cmds:$(CPU_DIRECTORY):$(CURDIR)/../hal

%.o: %.c
	$(compile)

%.o: %.S
	$(assemble)

clean:
	$(RM) $(OBJECTS) firmware.elf firmware.bin firmware.elf.map .*~ *~

.PHONY: all clean
