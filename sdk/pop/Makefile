# Prince of Persia (SDLPoP) for the riscv-stack SDK.
#
#   make            -> pop.bin       (copy to SD, pick in the Game slot)
#   make pop.pak    -> data pak      (copy to SD, pick in the Pak slot)
#   make pop-pc     -> native PC-twin binary (desktop SDL2; the dev loop)
#
# Engine sources are GPL-3.0-or-later (SDLPoP, NagyD/SDLPoP@3c5add5; see
# src/COPYING). The SDK stays BSD. Every edit to a vendored src/ file is
# tagged `RVSTACK:` so `git diff` of src/ IS the port.

GAME = pop

# Stock SDLPoP sources (src/), minus:
#   stb_vorbis.c  — Ogg music decode (float-heavy); music is MIDI->HW OPL3
#   opl3.c        — Nuked software OPL3; replaced by compat/opl3_hw.c
#   screenshot.c  — PNG writer (desktop-only)
#   lighting.c    — truecolor torch overlay (USE_ALPHA/USE_LIGHTING off)
POP_SRCS = \
	src/main.c src/data.c src/seg000.c src/seg001.c src/seg002.c \
	src/seg003.c src/seg004.c src/seg005.c src/seg006.c src/seg007.c \
	src/seg008.c src/seg009.c src/seqtbl.c src/replay.c src/options.c \
	src/menu.c src/midi.c

# Port seam (compat/): SDL2-on-sdl_lite shim, MIDI->HW-OPL3, pak-backed FILE
# streams, missing-libc pieces. See compat/SDL.h for the architecture notes.
COMPAT_SRCS = \
	compat/pop_sdl.c compat/lite_bridge.c compat/pop_png.c compat/lodepng.c \
	compat/opl3_hw.c compat/rvfile.c compat/vorbis_stub.c

# lodepng: decoder only (no encoder/disk/C++), keeps it lean for rv32.
LODEPNG_FLAGS = -DLODEPNG_NO_COMPILE_ENCODER -DLODEPNG_NO_COMPILE_DISK \
                -DLODEPNG_NO_COMPILE_CPP
CFLAGS += $(LODEPNG_FLAGS)

# Console-only fill-ins (real libc/libm on the PC twin already have these).
SHIM_SRCS = compat/libc_shim.c compat/math_shim.c

GAME_SRCS = $(POP_SRCS) $(COMPAT_SRCS) $(SHIM_SRCS)
PORTLIB   = pakfs sdl_lite

# game.mk needs the SoC build tree; guard so `make pak` / `make pop-pc` work
# without the LiteX toolchain checked out.
BUILD_DIR ?= $(CURDIR)/../../soc/build/pocket
ifneq ($(wildcard $(BUILD_DIR)/software/include/generated/variables.mak),)
include ../game.mk
endif

# Prepend include dirs so compat/'s shadow stdio.h and SDL.h win over
# picolibc's / anything else's.
CFLAGS := -I$(CURDIR)/compat -I$(CURDIR)/src $(CFLAGS)

# Match SDLPoP's release build; quiet the 2013-era decompiled-asm warnings.
CFLAGS += -DPOP_RVSTACK \
	-DNDEBUG \
	-Wno-missing-prototypes -Wno-unused-variable -Wno-unused-but-set-variable \
	-Wno-unused-function -Wno-sign-compare

# Data pak: the whole PoP data/ tree (loose resNNN.png/bin/pal in PRINCE/,
# TITLE/, LEVELS/, ... + the sound DATs). Keys lowercased (--lower) because
# pakfs match is case-sensitive and PoP mixes case; rvfile lowercases lookups.
pop.pak: $(shell find data -type f 2>/dev/null)
	python3 $(SDK_DIR)/../soc/tools/make_pakfs.py --lower data $@

pak: pop.pak
.PHONY: pak

# ── PC twin: the same game natively against desktop SDL2 ────────────────────
# libc_shim/math_shim excluded (real libc has them); sdl_lite's SDL_* names are
# RVL_-prefixed on PC (sdl_lite.h) and everything compat implements is psdl_-
# prefixed (compat/SDL.h) so real SDL2 keeps its namespace.
PC_GAME_SRCS = $(POP_SRCS) $(COMPAT_SRCS)
PC_SDK_SRCS  = ../pakfs.c ../sdl_lite.c ../pc/hal_pc.c

PC_FLAGS = -O2 -g -std=gnu99 -DRVSTACK_PC -DPOP_RVSTACK -DNDEBUG \
	-DLODEPNG_NO_COMPILE_ENCODER -DLODEPNG_NO_COMPILE_DISK -DLODEPNG_NO_COMPILE_CPP \
	$(shell pkg-config --cflags sdl2) \
	-Wno-missing-prototypes -Wno-unused-variable -Wno-unused-but-set-variable \
	-Wno-unused-function -Wno-sign-compare
PC_LIBS  = $(shell pkg-config --libs sdl2) -lm

# Two compile groups: game+compat see the shadow headers (-Icompat), the SDK
# and PC-twin HAL sources do NOT (they use real libc + real SDL2).
pop-pc: $(PC_GAME_SRCS) $(PC_SDK_SRCS)
	@mkdir -p pcobj
	@for f in $(PC_GAME_SRCS); do \
		o=pcobj/$$(basename $${f%.c}).o; \
		echo "CC (game) $$f"; \
		$(HOSTCC) -I$(CURDIR)/compat -I$(CURDIR)/src -I.. -I../../soc/hal \
			$(PC_FLAGS) -c $$f -o $$o || exit 1; \
	done; \
	for f in $(PC_SDK_SRCS); do \
		o=pcobj/sdk_$$(basename $${f%.c}).o; \
		echo "CC (sdk)  $$f"; \
		$(HOSTCC) $(PC_FLAGS) -I.. -I../../soc/hal -c $$f -o $$o || exit 1; \
	done
	$(HOSTCC) -o pop-pc pcobj/*.o $(PC_LIBS)

# host compiler for the PC twin (game.mk overrides CC with the RISC-V wrapper)
HOSTCC := gcc
.PHONY: pop-pc
