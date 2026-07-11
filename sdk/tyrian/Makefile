# OpenTyrian2000 for the riscv-stack SDK.
#
#   make            -> tyrian.bin  (copy to SD, pick in the Game slot)
#   make tyrian.pak -> data pak    (copy to SD, pick in the Pak slot)
#
# Game sources are GPL-2.0-or-later (see ATTRIBUTION.md); the SDK stays BSD.

GAME = tyrian

# TYRIAN_MUSIC=1 links the real DOSBox OPL synth + Loudness player (soft-
# float on rv32im: far too slow at 48 kHz today, but useful to benchmark).
# Default 0: compat/music_stub.c plays silence; PCM sound effects stay on.
TYRIAN_MUSIC ?= 0

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

ifeq ($(TYRIAN_MUSIC),1)
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
