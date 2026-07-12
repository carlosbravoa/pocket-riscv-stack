# Commander Keen 4 (Omnispeak) for the riscv-stack SDK.
#
#   make            -> keen.bin   (copy to SD, pick in the Game slot)
#   make pak        -> keen.pak   (shareware Keen 4 v1.4 EGA data + the
#                                  Omnispeak extraction files; Pak slot)
#   make keen-pc    -> native PC-twin binary (desktop SDL2 via hal_pc.c)
#
# Game sources are GPL-2.0 (src/LICENSE, Omnispeak by David Gow et al.);
# the port glue in compat/ accompanies them. The SDK stays BSD.
#
# The seam is hal.h-DIRECT (the Doom pattern, not the SDL-shim pattern):
# Omnispeak's own VL_Backend/IN_Backend/SD_Backend tables are implemented
# straight on the HAL in compat/id_{vl,in,sd}_rvstack.c, and its Filesystem
# Manager (id_fs.h) is reimplemented over pakfs + HAL saves in
# compat/id_fs_rvstack.c. Music/SFX are OPL register programs forwarded to
# the FM flavor's real OPL3 via opl_write (RVSTACK_OPLLOG captures the
# stream on the PC twin).

GAME = keen

# Stock Omnispeak sources minus id_fs.c (reimplemented over pakfs/saves in
# compat/) and minus the sdl/dos/null backends (ours live in compat/).
# icon.c is WITH_SDL-only and skipped. Keen 4 only (shareware data).
KEEN_SRCS = \
	src/ck_quit.c src/id_mm.c src/id_ca.c src/id_cfg.c src/id_in.c \
	src/id_rf.c src/id_sd.c src/id_ti.c src/id_us_1.c src/id_us_2.c \
	src/id_us_textscreen.c src/id_vh.c src/id_vl.c src/id_str.c \
	src/ck4_map.c src/ck4_obj1.c src/ck4_obj2.c src/ck4_obj3.c \
	src/ck4_misc.c \
	src/ck_act.c src/ck_inter.c src/ck_keen.c src/ck_obj.c src/ck_map.c \
	src/ck_phys.c src/ck_game.c src/ck_play.c src/ck_misc.c src/ck_main.c \
	src/ck_text.c src/ck_cross.c

# Port seam (compat/): the three Omnispeak backends + the FS manager,
# all hal.h-direct. See compat/rv_keen.h for the architecture notes.
COMPAT_SRCS = \
	compat/id_vl_rvstack.c compat/id_in_rvstack.c \
	compat/id_sd_rvstack.c compat/id_fs_rvstack.c

# Console-only: pieces LiteX's picolibc-minimal lacks (gamelib already
# provides malloc/mem*/rand — the shim deliberately avoids those).
SHIM_SRCS = compat/libc_shim.c

GAME_SRCS = $(KEEN_SRCS) $(COMPAT_SRCS) $(SHIM_SRCS)
PORTLIB   = pakfs

KEEN_DEFS = -DWITH_KEEN4

# Console target needs the LiteX build tree (soc/build/...); guard it so
# `make keen-pc` / `make pak` work on a machine without the toolchain.
BUILD_DIR ?= ../../soc/build/pocket
ifneq ($(wildcard $(BUILD_DIR)/software/include/generated/variables.mak),)
include ../game.mk

CFLAGS := -I$(CURDIR)/compat -I$(CURDIR)/src $(CFLAGS)

# NDEBUG quiets the asserts; the rest quiets 2012-era C under LiteX's
# -Wall + strict-prototype flags.
CFLAGS += -DNDEBUG -DRVSTACK $(KEEN_DEFS) \
	-Wno-missing-prototypes -Wno-unused-variable -Wno-unused-but-set-variable \
	-Wno-strict-prototypes -Wno-old-style-definition
else
SDK_DIR := ..
endif

# Data pak: shareware Keen 4 v1.4 EGA episode + Omnispeak's extraction
# files (EGAHEAD/EGADICT/ACTION/... .CK4), pakfs format.
keen.pak: $(wildcard data/*)
	python3 $(SDK_DIR)/../soc/tools/make_pakfs.py data $@

pak: keen.pak
.PHONY: pak

# ── PC twin: the same game natively against desktop SDL2 ────────────────────
# hal.h-direct means the PC twin is just hal_pc.c + pakfs.c — no sdl_lite.
# libc_shim excluded (real libc has it all).
PC_GAME_SRCS = $(KEEN_SRCS) $(COMPAT_SRCS)
PC_SDK_SRCS  = ../pakfs.c ../pc/hal_pc.c
PC_FLAGS     = -O2 -g -std=gnu99 -DRVSTACK_PC -DNDEBUG $(KEEN_DEFS) \
	$(shell pkg-config --cflags sdl2) \
	-Wno-unused-variable -Wno-unused-but-set-variable

keen-pc: $(PC_GAME_SRCS) $(PC_SDK_SRCS)
	@mkdir -p pcobj
	@for s in $(PC_GAME_SRCS); do \
	  gcc -Icompat -Isrc -I.. -I../../soc/hal $(PC_FLAGS) \
	      -c $$s -o pcobj/$$(basename $$s .c).o || exit 1; done
	@for s in $(PC_SDK_SRCS); do \
	  gcc $(PC_FLAGS) -I.. -I../../soc/hal \
	      -c $$s -o pcobj/sdk_$$(basename $$s .c).o || exit 1; done
	gcc -o $@ pcobj/*.o $(shell pkg-config --libs sdl2) -lm
.PHONY: keen-pc
