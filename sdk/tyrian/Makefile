# OpenTyrian2000 for the riscv-stack SDK.
#
#   make            -> tyrian.bin  (copy to SD, pick in the Game slot)
#   make tyrian.pak -> data pak    (copy to SD, pick in the Pak slot)
#
# Game sources are GPL-2.0-or-later (see ATTRIBUTION.md); the SDK stays BSD.

GAME = tyrian

# TYRIAN_MUSIC selects the music backend:
#   hw (default) — lds_play sequencer drives the FM flavor's REAL OPL3 via
#                  opl_write (compat/opl3_hw.c); silent no-op on flavors
#                  without FM. Zero CPU synthesis.
#   1            — DOSBox soft-OPL (float; hopeless on rv32im, benchmarking)
#   0            — full stub (no music engine at all)
TYRIAN_MUSIC ?= hw

# Stock OpenTyrian2000 sources (src/), minus:
#   video_scale*.c  — RGB software scalers (native 320x200 indexed path
#                     instead; compat/video_scale_stub.c keeps the menu alive)
#   opl.c/lds_play.c — see TYRIAN_MUSIC above
TYRIAN_SRCS = \
	src/animlib.c src/arg_parse.c src/backgrnd.c src/config.c \
	src/config_file.c src/destruct.c src/editship.c src/episodes.c \
	src/file.c src/font.c src/fonthand.c src/game_menu.c src/helptext.c \
	src/joystick.c src/jukebox.c src/keyboard.c \
	src/loudness.c src/lvllib.c src/lvlmast.c src/mainint.c src/menus.c \
	src/mouse.c src/mtrand.c src/musmast.c src/network.c src/nortsong.c \
	src/nortvars.c src/opentyr.c src/palette.c src/params.c \
	src/pcxload.c src/pcxmast.c src/picload.c src/player.c src/shots.c \
	src/sizebuf.c src/sndmast.c src/sprite.c src/starlib.c src/tyrian2.c \
	src/varz.c src/vga256d.c src/vga_palette.c src/video.c src/xmas.c

ifeq ($(TYRIAN_MUSIC),hw)
TYRIAN_SRCS += src/lds_play.c compat/opl3_hw.c
else ifeq ($(TYRIAN_MUSIC),1)
TYRIAN_SRCS += src/opl.c src/lds_play.c
else
TYRIAN_SRCS += compat/music_stub.c
endif

# Port seam (compat/): SDL2-on-sdl_lite shim, pakfs-backed FILE streams,
# missing-libc pieces. See compat/SDL.h for the architecture notes.
COMPAT_SRCS = \
	compat/lite_bridge.c compat/sdl2_shim.c compat/of_files.c \
	compat/rvfile.c compat/libc_shim.c compat/math_shim.c \
	compat/video_scale_stub.c

GAME_SRCS = $(TYRIAN_SRCS) $(COMPAT_SRCS)
PORTLIB   = pakfs sdl_lite

include ../game.mk

# Prepend include dirs so compat/'s shadow stdio.h and SDL.h win over
# picolibc's/anything else's (game.mk/common.mak put theirs first otherwise).
CFLAGS := -I$(CURDIR)/compat -I$(CURDIR)/src $(CFLAGS)

# Match upstream's release defines (NDEBUG also disables asserts that are
# stricter than the Tyrian 2000 data files). Keep the warning noise down:
# this is 46k lines of 2007-era C compiled with LiteX's -Wall + strict-proto
# flags.
CFLAGS += -DNDEBUG \
	-Wno-missing-prototypes -Wno-unused-variable -Wno-unused-but-set-variable

# Data pak: every (kept) Tyrian 2000 freeware data file, pakfs format.
tyrian.pak: $(wildcard data/*)
	python3 $(SDK_DIR)/../soc/tools/make_pakfs.py data $@

pak: tyrian.pak
.PHONY: pak

# ── PC twin: the same game natively against desktop SDL2 ────────────────────
# libc_shim/math_shim excluded (real libc has it all); sdl_lite's SDL_* names
# are RVL_-prefixed on PC (see sdl_lite.h) so real SDL2 keeps its namespace.
PC_SRCS = $(TYRIAN_SRCS) \
	compat/lite_bridge.c compat/sdl2_shim.c compat/of_files.c \
	compat/rvfile.c compat/video_scale_stub.c \
	../pakfs.c ../sdl_lite.c ../pc/hal_pc.c

# Two compile groups: game+compat see the shadow headers (-Icompat), the
# SDK/PC-twin sources must NOT (their fopen is the real libc one).
PC_GAME_SRCS = $(TYRIAN_SRCS) \
	compat/lite_bridge.c compat/sdl2_shim.c compat/of_files.c \
	compat/rvfile.c compat/video_scale_stub.c
PC_SDK_SRCS  = ../pakfs.c ../sdl_lite.c ../pc/hal_pc.c
PC_FLAGS     = -O2 -g -std=gnu99 -DRVSTACK_PC -DNDEBUG \
	$(shell pkg-config --cflags sdl2) \
	-Wno-unused-variable -Wno-unused-but-set-variable

tyrian-pc: $(PC_GAME_SRCS) $(PC_SDK_SRCS)
	@mkdir -p pcobj
	@for s in $(PC_GAME_SRCS); do \
	  gcc -Icompat -Isrc -I.. -I../../soc/hal $(PC_FLAGS) \
	      -c $$s -o pcobj/$$(basename $$s .c).o || exit 1; done
	@for s in $(PC_SDK_SRCS); do \
	  gcc $(PC_FLAGS) -I.. -I../../soc/hal \
	      -c $$s -o pcobj/sdk_$$(basename $$s .c).o || exit 1; done
	gcc -o $@ pcobj/*.o $(shell pkg-config --libs sdl2) -lm
.PHONY: tyrian-pc
