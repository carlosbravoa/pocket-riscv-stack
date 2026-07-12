# Quabricks (the revamped sdl2-tetris) for the riscv-stack SDK — the port
# that grew sdl2_lite's audio, blending and scaled-text corners.
#
#   make               -> quabricks.bin  (copy to SD, pick in the Game slot;
#                                         needs the SoC build tree + RISC-V
#                                         toolchain — see PORTING.md)
#   make quabricks-pc  -> PC twin against desktop SDL2 (develop/play here)
#
# No pak: fonts render via the shim's built-in 8x8 font and the SFX are baked
# into the binary (compat/sfx_data.c, from tools/wav2c.py). Game sources are
# MIT (see LICENSE).

GAME = quabricks
GAME_SRCS = src/tetris.c src/graphics.c src/input.c src/hiscore.c \
            src/sound.c src/logsys.c compat/sfx_data.c
PORTLIB   = sdl2_lite

# Console-only libc pieces (snprintf — picolibc-minimal has none); the PC
# twin links the real libc instead.
CONSOLE_ONLY_SRCS = compat/libc_shim.c

# game.mk needs the SoC build tree (variables.mak). Guarded so `make
# quabricks-pc` still works in a checkout that never ran the SoC build.
BUILD_DIR ?= ../../soc/build/pocket
ifneq ($(wildcard $(BUILD_DIR)/software/include/generated/variables.mak),)
GAME_SRCS += $(CONSOLE_ONLY_SRCS)
include ../game.mk

# Prepend the shadow-header dir so <SDL2/*.h> resolves to compat/SDL2/
# (the sdl2_lite shim), tyrian-style.
CFLAGS := -I$(CURDIR)/compat $(CFLAGS)
else
all:
	@echo "No SoC build tree at $(BUILD_DIR) — console build unavailable."
	@echo "PC twin: make quabricks-pc"
.PHONY: all
endif

# ── PC twin: the same game natively against desktop SDL2 ────────────────────
# Two compile groups (the tyrian recipe): game sources see the shadow
# <SDL2/*> headers and NO real SDL2 include path; hal_pc.c gets the real
# thing. sdl2_lite's exports are RVSDL2_-prefixed (see sdk/sdl2_lite.h), so
# libSDL2 keeps the SDL_* link namespace.
PC_GAME_SRCS = $(GAME_SRCS) ../sdl2_lite.c
PC_HAL_SRC   = ../pc/hal_pc.c
PC_FLAGS     = -O2 -g -std=gnu99 -Wall -DRVSTACK_PC

quabricks-pc: $(PC_GAME_SRCS) $(PC_HAL_SRC) ../sdl2_lite.h compat/SDL2/SDL.h
	@mkdir -p pcobj
	@for s in $(filter-out $(CONSOLE_ONLY_SRCS),$(PC_GAME_SRCS)); do \
	  gcc -Icompat -I.. -I../../soc/hal $(PC_FLAGS) \
	      -c $$s -o pcobj/$$(basename $$s .c).o || exit 1; done
	gcc $(PC_FLAGS) -I../../soc/hal $(shell pkg-config --cflags sdl2) \
	    -c $(PC_HAL_SRC) -o pcobj/hal_pc.o
	gcc -o $@ pcobj/*.o $(shell pkg-config --libs sdl2)
.PHONY: quabricks-pc

# Regenerate the embedded SFX after touching data/sfx or tools/gen_sfx.py
sfx:
	python3 tools/wav2c.py
.PHONY: sfx

pc-clean:
	rm -rf pcobj quabricks-pc
.PHONY: pc-clean
