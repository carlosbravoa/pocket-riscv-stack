# Wolfenstein 3D (Wolf4SDL) for the riscv-stack SDK.
#
#   make            -> wolf3d.bin  (copy to SD, pick in the Game slot)
#   make pak        -> wolf3d.pak  (shareware .WL1 data; pick in the Pak slot)
#   make wolf3d-pc  -> native PC-twin binary (desktop SDL2; the dev loop)
#
# Game sources are the id Software license (src/license-id.txt); the port
# glue in compat/ accompanies them. The SDK stays BSD.
#
# The showpiece: Wolf3D music is IMF — raw OPL register+delay dumps — and
# compat/id_sd_rv.c feeds them to the FM flavor's REAL OPL3 via opl_write.
# On the PC twin, RVSTACK_OPLLOG=file captures that register stream.

GAME = wolf3d

# Stock Wolf4SDL v2.0 sources minus id_sd.c (sound manager reimplemented
# over the HAL in compat/id_sd_rv.c: real OPL3 + pumped-callback mixer).
WOLF_SRCS = \
	src/id_ca.c src/id_in.c src/id_pm.c src/id_us.c src/id_vh.c \
	src/id_vl.c src/signon.c src/wl_act1.c src/wl_act2.c src/wl_agent.c \
	src/wl_atmos.c src/wl_cloudsky.c src/wl_debug.c src/wl_draw.c \
	src/wl_game.c src/wl_inter.c src/wl_main.c src/wl_menu.c \
	src/wl_parallax.c src/wl_plane.c src/wl_play.c src/wl_scale.c \
	src/wl_shade.c src/wl_state.c src/wl_text.c src/wl_utils.c

# Port seam (compat/): SDL2-on-sdl_lite shim, pakfs-backed FILE streams,
# the HAL sound manager. See compat/SDL.h for the architecture notes.
COMPAT_SRCS = \
	compat/lite_bridge.c compat/wsdl.c compat/id_sd_rv.c compat/rvfile.c

# Console-only: pieces LiteX's picolibc-minimal lacks.
SHIM_SRCS = compat/libc_shim.c compat/math_shim.c

GAME_SRCS = $(WOLF_SRCS) $(COMPAT_SRCS) $(SHIM_SRCS)
PORTLIB   = pakfs sdl_lite

# Console target needs the LiteX build tree (soc/build/...); guard it so
# `make wolf3d-pc` / `make pak` work on a machine without the toolchain.
ifneq ($(wildcard ../../soc/build/pocket/software/include/generated/variables.mak),)
include ../game.mk

# Prepend include dirs so compat/'s shadow stdio.h and SDL.h win over
# picolibc's (game.mk puts its own first otherwise).
CFLAGS := -I$(CURDIR)/compat -I$(CURDIR)/src $(CFLAGS)

# NDEBUG: compile out asserts (id engine asserts are hot-path); the rest
# quiets 1992-era C under LiteX's -Wall + strict-prototype flags.
CFLAGS += -DNDEBUG \
	-Wno-missing-prototypes -Wno-unused-variable -Wno-unused-but-set-variable
else
SDK_DIR := ..
endif

# Data pak: the shareware episode files (data/*.wl1), pakfs format.
wolf3d.pak: $(wildcard data/*)
	python3 $(SDK_DIR)/../soc/tools/make_pakfs.py data $@

pak: wolf3d.pak
.PHONY: pak

# ── PC twin: the same game natively against desktop SDL2 ────────────────────
# libc_shim/math_shim excluded (real libc has it all); sdl_lite's SDL_* names
# are RVL_-prefixed on PC (see sdl_lite.h) and everything compat implements
# is wsdl_-prefixed on BOTH targets, so real SDL2 keeps its namespace.
PC_GAME_SRCS = $(WOLF_SRCS) $(COMPAT_SRCS)
PC_SDK_SRCS  = ../pakfs.c ../sdl_lite.c ../pc/hal_pc.c
PC_FLAGS     = -O2 -g -std=gnu99 -DRVSTACK_PC -DNDEBUG \
	$(shell pkg-config --cflags sdl2) \
	-Wno-unused-variable -Wno-unused-but-set-variable

# Two compile groups: game+compat see the shadow headers (-Icompat), the
# SDK/PC-twin sources must NOT (their fopen is the real libc one).
wolf3d-pc: $(PC_GAME_SRCS) $(PC_SDK_SRCS)
	@mkdir -p pcobj
	@for s in $(PC_GAME_SRCS); do \
	  gcc -Icompat -Isrc -I.. -I../../soc/hal $(PC_FLAGS) \
	      -c $$s -o pcobj/$$(basename $$s .c).o || exit 1; done
	@for s in $(PC_SDK_SRCS); do \
	  gcc $(PC_FLAGS) -I.. -I../../soc/hal \
	      -c $$s -o pcobj/sdk_$$(basename $$s .c).o || exit 1; done
	gcc -o $@ pcobj/*.o $(shell pkg-config --libs sdl2) -lm
.PHONY: wolf3d-pc
