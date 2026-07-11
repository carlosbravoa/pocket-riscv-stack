#!/usr/bin/env bash
# Export a RELOCATABLE, self-contained SDK: build games anywhere with only the
# riscv-none-elf-gcc toolchain on PATH — no LiteX, no repo, no Quartus.
#
# Bundles: hal sources, game link scripts, all headers the HAL pulls in
# (generated CSRs, LiteX soc/software tree, picolibc, CPU system.h), the
# prebuilt static libs, the examples, and a standalone Makefile.
#
# Usage: ./export_sdk.sh [outdir]     (default ./sdk-dist, plus a .tar.gz)
#
# SPDX-License-Identifier: BSD-2-Clause
set -euo pipefail
cd "$(dirname "$0")"

OUT="${1:-$PWD/sdk-dist}"
REPO="$(cd .. && pwd)"
BUILD="$REPO/soc/build/pocket"
VMAK="$BUILD/software/include/generated/variables.mak"
[ -f "$VMAK" ] || { echo "FATAL: build the SoC first (soc/build/pocket missing)"; exit 1; }

get() { sed -n "s/^$1=//p" "$VMAK"; }
PICOLIBC=$(get PICOLIBC_DIRECTORY)
CPU_DIR=$(get CPU_DIRECTORY)
SOC_DIR=$(get SOC_DIRECTORY)
LITEDRAM_SW_DIR=$(sed -n "s/^LIBLITEDRAM_DIRECTORY=//p" "$BUILD/software/include/generated/variables.mak")

rm -rf "$OUT"
mkdir -p "$OUT"/{include,lib,hal,examples}

echo "== headers =="
cp -r "$BUILD/software/include/generated" "$OUT/include/generated"
cp -r "$SOC_DIR/software/include/." "$OUT/include/"          # base/ etc.
mkdir -p "$OUT/include/litex-sw"
cp -r "$SOC_DIR/software/libbase" "$OUT/include/litex-sw/libbase"
cp -r "$LITEDRAM_SW_DIR" "$OUT/include/litex-sw/liblitedram"
cp "$CPU_DIR"/*.h "$OUT/include/"                            # system.h, csr-defs.h, irq.h
mkdir -p "$OUT/include/picolibc"
cp -r "$PICOLIBC/newlib/libc/include/." "$OUT/include/picolibc/" 2>/dev/null \
  || cp -r "$PICOLIBC/libc/include/." "$OUT/include/picolibc/"
cp "$BUILD/software/libc/picolibc.h" "$OUT/include/picolibc/"   # generated config

echo "== libs =="
for l in libc libcompiler_rt libbase liblitedram; do
  cp "$BUILD/software/$l/$l.a" "$OUT/lib/"
done

echo "== sdk core + hal + examples =="
cp game.ld crt0_game.S gamelib.c font8x8_basic.h GUIDE.md "$OUT/"
cp pakfs.h pakfs.c sdl_lite.h sdl_lite.c "$OUT/"               # portlib
mkdir -p "$OUT/pc"; cp pc/hal_pc.c pc/pc.mk "$OUT/pc/"         # PC twin
cp "$REPO/soc/tools/make_pakfs.py" "$OUT/tools/" 2>/dev/null \
  || { mkdir -p "$OUT/tools"; cp "$REPO/soc/tools/make_pakfs.py" "$OUT/tools/"; }
cp "$REPO/soc/hal/hal.h" "$REPO/soc/hal/hal.c" "$OUT/hal/"
cp -r demo pong "$OUT/examples/"
mkdir -p "$OUT/examples/pakfstest"
cp pakfstest/main.c pakfstest/Makefile "$OUT/examples/pakfstest/"
rm -f "$OUT"/examples/*/[a-z]*.o "$OUT"/examples/*/*.d \
      "$OUT"/examples/*/*.elf* "$OUT"/examples/*/*.bin 2>/dev/null || true
sed -i 's|include ../game.mk|include ../../game.mk|' "$OUT"/examples/*/Makefile

echo "== standalone Makefile =="
cat > "$OUT/game.mk" <<'MK'
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
MK

cat > "$OUT/README.md" <<'RM'
# RISC-V Stack SDK (standalone export)

Build games for the RISC-V Stack Pocket core with nothing but
xpack riscv-none-elf-gcc on PATH:

    cd examples/pong && make      # -> pong.bin
    # copy to the Pocket SD card, pick it in the core's Game slot

Start with GUIDE.md. New game: copy examples/pong, edit its Makefile
(GAME / GAME_SRCS) and go.
RM

echo "== tarball =="
TAR="$PWD/riscv-stack-sdk.tar.gz"
tar czf "$TAR" -C "$(dirname "$OUT")" "$(basename "$OUT")"
ls -lh "$TAR"
echo "== done: $OUT =="
