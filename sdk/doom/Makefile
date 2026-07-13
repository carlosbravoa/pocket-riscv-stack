# DOOM (doomgeneric) for the riscv-stack SDK.
#
#   make            -> doom.bin   (copy to SD, pick in the Game slot)
#   make wad        -> fetch the freely-distributable shareware doom1.wad
#   make pak        -> doom.pak   (copy to SD, pick in the Pak slot)
#   make doom-pc    -> PC twin: the same port natively on desktop SDL2
#
# Game sources are GPL-2.0-or-later (see ATTRIBUTION.md); the SDK stays BSD.

GAME = doom

# Everything platform-shaped funnels through doomgeneric's DG_* seam
# (compat/dg_rvstack.c) — no per-OS backend from upstream is built.
# CMAP256 keeps the whole pipeline 8-bit palettized: Doom's renderer is
# indexed already, DG_ScreenBuffer stays index bytes, and PLAYPAL goes to
# the hardware CLUT via palette_set(). RESX/RESY 320x200 = native, no
# scaling; dg_rvstack letterboxes onto the 320x240 framebuffer.
# FEATURE_SOUND selects compat/i_rvsound.c (8-voice SFX mixer on the HAL's
# 48 kHz stream, pumped once per frame from DG_DrawFrame); i_sound.c's
# SDL_mixer include is patched out (see the RVSTACK note there). Music is
# compat/i_rvmusic.c: MUS sequencer -> vendored oplgm GM performer -> the
# FM flavor's real OPL3, patches from the WAD's GENMIDI lump (bank.c).
# HAL_FEAT_FM-gated: PCM-only flavors get SFX and silent music.
DOOM_DEFS = -DCMAP256 -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 \
	-DFEATURE_SOUND

# Stock doomgeneric sources (src/) — upstream's object list verbatim, minus
# the platform backend (doomgeneric_xlib there, compat/dg_rvstack.c here).
# FEATURE_SOUND stays off for milestone 1: i_sound.c compiles to a clean
# no-sound dispatch, no SDL_mixer wanted.
DOOM_SRCS = \
	src/dummy.c src/am_map.c src/doomdef.c src/doomstat.c src/dstrings.c \
	src/d_event.c src/d_items.c src/d_iwad.c src/d_loop.c src/d_main.c \
	src/d_mode.c src/d_net.c src/f_finale.c src/f_wipe.c src/g_game.c \
	src/hu_lib.c src/hu_stuff.c src/info.c src/i_cdmus.c src/i_endoom.c \
	src/i_joystick.c src/i_scale.c src/i_sound.c src/i_system.c \
	src/i_timer.c src/memio.c src/m_argv.c src/m_bbox.c src/m_cheat.c \
	src/m_config.c src/m_controls.c src/m_fixed.c src/m_menu.c \
	src/m_misc.c src/m_random.c src/p_ceilng.c src/p_doors.c \
	src/p_enemy.c src/p_floor.c src/p_inter.c src/p_lights.c src/p_map.c \
	src/p_maputl.c src/p_mobj.c src/p_plats.c src/p_pspr.c src/p_saveg.c \
	src/p_setup.c src/p_sight.c src/p_spec.c src/p_switch.c \
	src/p_telept.c src/p_tick.c src/p_user.c src/r_bsp.c src/r_data.c \
	src/r_draw.c src/r_main.c src/r_plane.c src/r_segs.c src/r_sky.c \
	src/r_things.c src/sha1.c src/sounds.c src/statdump.c src/st_lib.c \
	src/st_stuff.c src/s_sound.c src/tables.c src/v_video.c \
	src/wi_stuff.c src/w_checksum.c src/w_file.c src/w_file_stdc.c \
	src/w_main.c src/w_wad.c src/z_zone.c src/i_input.c src/i_video.c \
	src/doomgeneric.c

# Port seam (compat/): DG_* over hal.h, pakfs-backed FILE streams via the
# shadow stdio.h, missing-libc pieces (console only). See compat/rvfile.c.
COMPAT_SRCS = compat/dg_rvstack.c compat/rvfile.c compat/i_rvsound.c \
	compat/i_rvmusic.c compat/bank.c compat/oplgm.c compat/libc_shim.c

GAME_SRCS = $(DOOM_SRCS) $(COMPAT_SRCS)
PORTLIB   = pakfs

include ../game.mk

# Prepend include dirs so compat/'s shadow stdio.h (and unistd/fcntl/
# sys/stat stand-ins) win over picolibc's (game.mk/common.mak put theirs
# first otherwise).
CFLAGS := -I$(CURDIR)/compat -I$(CURDIR)/src $(CFLAGS)

# NDEBUG matches upstream release builds; the warning switches quiet
# 1993-era C under LiteX's -Wall + strict-prototype flags.
CFLAGS += $(DOOM_DEFS) -DNDEBUG \
	-Wno-missing-prototypes -Wno-unused-variable -Wno-unused-but-set-variable

# ── Assets ───────────────────────────────────────────────────────────────
# The shareware doom1.wad (id Software, freely distributable) is the test
# IWAD. Not committed to the repo: fetch it, then pack the pak.
WAD_URL = https://distro.ibiblio.org/slitaz/sources/packages/d/doom1.wad

assets/doom1.wad:
	@mkdir -p assets
	curl -fL -o $@ $(WAD_URL)
	@ls -l $@

wad: assets/doom1.wad
.PHONY: wad

doom.pak: assets/doom1.wad
	python3 $(SDK_DIR)/../soc/tools/make_pakfs.py assets $@

pak: doom.pak
.PHONY: pak

# ── PC twin: the same game natively against desktop SDL2 ────────────────────
# Two compile groups (the Tyrian pattern): game+compat see the shadow
# headers (-Icompat) so ALL file I/O runs through pakfs/HAL saves — the
# twin tests the seam, not the host filesystem; the SDK/PC-twin sources
# must NOT (their fopen is the real libc one). libc_shim.c is console-only.
PC_GAME_SRCS = $(DOOM_SRCS) compat/dg_rvstack.c compat/rvfile.c \
	compat/i_rvsound.c compat/i_rvmusic.c compat/bank.c compat/oplgm.c
PC_SDK_SRCS  = ../pakfs.c ../pc/hal_pc.c
PC_FLAGS     = -O2 -g -std=gnu99 -DRVSTACK_PC $(DOOM_DEFS) -DNDEBUG \
	$(shell pkg-config --cflags sdl2) \
	-Wno-unused-variable -Wno-unused-but-set-variable

doom-pc: $(PC_GAME_SRCS) $(PC_SDK_SRCS)
	@mkdir -p pcobj
	@for s in $(PC_GAME_SRCS); do \
	  gcc -Icompat -Isrc -I.. -I../../soc/hal $(PC_FLAGS) \
	      -c $$s -o pcobj/$$(basename $$s .c).o || exit 1; done
	@for s in $(PC_SDK_SRCS); do \
	  gcc $(PC_FLAGS) -I.. -I../../soc/hal \
	      -c $$s -o pcobj/sdk_$$(basename $$s .c).o || exit 1; done
	gcc -o $@ pcobj/*.o $(shell pkg-config --libs sdl2) -lm
.PHONY: doom-pc
