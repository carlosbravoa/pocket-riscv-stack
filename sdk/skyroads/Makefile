# SkyRoads (Skyroads-c) for the riscv-stack SDK.
#
#   make               -> skyroads.bin  (copy to SD, pick in the Game slot)
#   make pak           -> skyroads.pak  (copy to SD, or auto-bound by name)
#   make skyroads-pc   -> native PC-twin binary (the dev loop)
#
# Game sources are vendored from Skyroads-c (~/devel/fpga/games/Skyroads-c,
# MIT; see src/LICENSE and UPSTREAM_COMMIT): a faithful C++17 reimplementation
# of the 1993 Bluemoon DOS game that loads the original freeware data files.
# Every edit to a vendored src/ file is tagged `RVSTACK:` so `git diff` of
# src/ IS the port. The SDK glue stays BSD-2-Clause.

GAME = skyroads

# Vendored layers: data (asset loaders), core (app + deterministic ship sim),
# renderer (indexed TREKDAT rasterizer), audio (AdLib driver + MUZAX
# sequencer). NOT vendored: src/platform (SDL2 host -> compat/host.cpp),
# src/cli (desktop inspection tool), src/audio/opl_chip.cpp + third_party/ymfm
# (software YM3812 -> compat/opl_chip_hw.cpp, the real OPL3).
SKY_SRCS := $(sort $(wildcard src/data/*.cpp) $(wildcard src/core/*.cpp) \
	$(wildcard src/renderer/*.cpp)) \
	src/audio/audio.cpp

# Port seam (compat/): hal-direct host, pak-backed assets, hardware OPL,
# fire-and-forget PCM SFX.
COMPAT_SRCS = \
	compat/host.cpp compat/pak_assets.cpp compat/audio_rv.cpp \
	compat/opl_chip_hw.cpp

# Console-only: freestanding C++ runtime pieces + picolibc gap fillers
# (the twin gets all of this from the host's real libstdc++/glibc).
CONSOLE_ONLY_SRCS = compat/cxx_rt.cpp compat/libc_shim.c compat/math_shim.c \
	compat/rv_shim.c

GAME_SRCS = $(SKY_SRCS) $(COMPAT_SRCS) $(CONSOLE_ONLY_SRCS)
PORTLIB   = pakfs

BUILD_DIR ?= $(CURDIR)/../../soc/build/pocket
ifneq ($(wildcard $(BUILD_DIR)/software/include/generated/variables.mak),)
include ../game.mk
# Upstream is C++17 (std::optional through the data/core layers); game.mk's
# default is the OpenJazz-era gnu++11. Redefined recursively — game.mk's
# common.mak rewrites CFLAGS, so anything frozen earlier is lost.
# The picolibc include dirs are dropped for C++: xpack's libstdc++ headers are
# built against ITS newlib headers and the two libcs' guards/typedefs clash
# (__FILE, malloc-in-::). C++ units see newlib headers; the link stays on
# libc_game.a (picolibc) — the seam passes only scalars, and gc-sections
# drops the FILE*-based path loaders the console never calls.
# int32_t is `long int` on rv32 newlib but `int` everywhere upstream builds,
# which breaks every std::max(int32_t, 1) deduction. Same size, same ABI —
# align the type instead of editing dozens of call sites.
INT32_AS_INT = -U__INT32_TYPE__ -D__INT32_TYPE__=int \
               -U__UINT32_TYPE__ "-D__UINT32_TYPE__=unsigned int"
OJ_CXXFLAGS = $(foreach f,$(filter-out -std=gnu99,$(CFLAGS)),\
                $(if $(findstring picolibc,$(f)),,$(f))) \
              $(INT32_AS_INT) \
              -fno-threadsafe-statics -fno-use-cxa-atexit -std=gnu++17
# std::string/vector out-of-line pieces live in libstdc++ (superset of the
# libsupc++ default).
GAME_CXXLIBS = -lstdc++ -lgcc -lm
endif

# After the include: common.mak assigns CFLAGS, so additions must follow it.
CFLAGS := -I$(CURDIR)/compat -I$(CURDIR)/src $(CFLAGS)
CFLAGS += -DRVSTACK -DNDEBUG

# Flag-variant guard: make does NOT rebuild when CFLAGS change, so a plain
# `make` after a `make TIMING=1` silently RELINKS the instrumented objects
# into the release binary (this shipped a debug overlay to hardware once).
# Any variant build stamps a marker; a mismatch forces a clean.
VARIANT := $(if $(TIMING),timing,)$(if $(SELFTEST),selftest,)$(if $(or $(TIMING),$(SELFTEST)),,release)
-include .variant
ifneq ($(LAST_VARIANT),$(VARIANT))
$(shell $(MAKE) -s clean >/dev/null 2>&1; echo "LAST_VARIANT := $(VARIANT)" > .variant)
endif

# `make TIMING=1` = the hardware-profiling build: per-stage microsecond
# counters drawn in the top letterbox bar (see compat/host.cpp SKY_TIMING).
ifdef TIMING
CFLAGS += -DSKY_TIMING
endif

# `make SELFTEST=1` -> plays the bounce through the game's own audio layer at
# boot (blue screen), for A/B against sdk/sfxtest on hardware.
ifdef SELFTEST
CFLAGS += -DSKY_AUDIO_SELFTEST
endif

# Data pak: the original SkyRoads freeware files, lowercased (pakfs match is
# case-sensitive; pak_assets asks lowercase).
skyroads.pak: $(wildcard data/*)
	python3 ../../soc/tools/make_pakfs.py data $@

pak: skyroads.pak
.PHONY: pak

# ── PC twin: the same game natively, against the same compat seam ──────────
# The SAME host/audio/pak code as the console on sdk/pc's hal_pc (SDL2 under
# the hood): one code path, so a twin bug is a console bug. Music is register-
# logged only on the twin (hal_pc opl_write -> RVSTACK_OPLLOG); it sounds on
# the FM console and the FM sim.
PC_GAME_SRCS = $(SKY_SRCS) $(COMPAT_SRCS)
PC_SDK_SRCS  = ../pakfs.c ../pc/hal_pc.c

PC_INC = -I$(CURDIR)/compat -I$(CURDIR)/src -I.. -I../../soc/hal

PC_FLAGS = -O2 -g -DRVSTACK_PC -DRVSTACK -DNDEBUG \
	$(shell pkg-config --cflags sdl2)
PC_CXXFLAGS = $(PC_FLAGS) -std=gnu++17
PC_LIBS  = $(shell pkg-config --libs sdl2) -lm

HOSTCC  := gcc
HOSTCXX := g++

skyroads-pc: $(PC_GAME_SRCS) $(PC_SDK_SRCS)
	@mkdir -p pcobj
	@for f in $(PC_GAME_SRCS); do \
		o=pcobj/$$(echo $${f%.*} | tr / _).o; \
		echo "CXX (game) $$f"; \
		$(HOSTCXX) $(PC_INC) $(PC_CXXFLAGS) -c $$f -o $$o || exit 1; \
	done; \
	for f in $(PC_SDK_SRCS); do \
		o=pcobj/sdk_$$(basename $${f%.c}).o; \
		echo "CC  (sdk)  $$f"; \
		$(HOSTCC) $(PC_FLAGS) -I.. -I../../soc/hal -c $$f -o $$o || exit 1; \
	done
	$(HOSTCXX) -o skyroads-pc pcobj/*.o $(PC_LIBS)

.PHONY: skyroads-pc
