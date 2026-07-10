# Standalone game build (exported SDK). From your game directory:
#   GAME = mygame
#   GAME_SRCS = main.c
#   include ../../game.mk        # path to this file
SDK_DIR := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
CROSS   ?= riscv-none-elf-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy

CPUFLAGS = -march=rv32i2p0_m -mabi=ilp32 -D__VexiiRiscv__ -D__riscv_zicbom__ -D__riscv_plic__
INCLUDES = -I$(SDK_DIR)/include/picolibc -I$(SDK_DIR)/include \
           -I$(SDK_DIR)/include/generated/.. -I$(SDK_DIR)/include/litex-sw \
           -I$(SDK_DIR)/include/litex-sw/libbase -I$(SDK_DIR)/hal -I$(SDK_DIR)
CFLAGS   = $(CPUFLAGS) $(INCLUDES) -Os -g3 -std=gnu99 -fomit-frame-pointer \
           -Wall -fno-builtin -fno-stack-protector -ffunction-sections -fdata-sections \
           -flto -fexceptions
LDFLAGS  = $(CPUFLAGS) -nostartfiles -nodefaultlibs -Wl,--build-id=none

GAME ?= game
OBJECTS = crt0_game.o gamelib.o hal.o $(GAME_SRCS:.c=.o)
VPATH   = $(SDK_DIR):$(SDK_DIR)/hal

all: $(GAME).bin

$(GAME).bin: $(GAME).elf
	$(OBJCOPY) -O binary $< $@
	@printf '\0\0\0\0' >> $@
	@ls -l $@

$(GAME).elf: $(OBJECTS) $(SDK_DIR)/game.ld
	$(CC) $(LDFLAGS) -T $(SDK_DIR)/game.ld -N -o $@ $(OBJECTS) \
	    -L$(SDK_DIR)/lib \
	    -Wl,--whole-archive -Wl,--start-group -lc -lcompiler_rt -lbase -llitedram \
	    -Wl,--end-group -Wl,--no-whole-archive -Wl,--gc-sections -Wl,-Map,$@.map

%.o: %.c
	$(CC) -c $(CFLAGS) -o $@ $<
%.o: %.S
	$(CC) -c $(CPUFLAGS) -DBASE_ISA_RV32I -o $@ $<

clean:
	$(RM) $(OBJECTS) $(GAME).elf $(GAME).bin $(GAME).elf.map
.PHONY: all clean
