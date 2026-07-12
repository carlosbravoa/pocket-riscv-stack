# sdl2-tetris for the riscv-stack SDK — the first SDL2 port, and the port
# that grew sdk/sdl2_lite (the SDL2 shim; sdl_lite's SDL2-shaped sibling).
#
#   make            -> tetris.bin  (copy to SD, pick in the Game slot;
#                                   needs the repo's SoC build tree + RISC-V
#                                   toolchain — see PORTING.md)
#   make tetris-pc  -> PC twin against desktop SDL2 (develop/play here)
#
# No pak: the game's only asset was a .ttf that the shim replaces with the
# SDK's built-in 8x8 font. Game sources are MIT (see LICENSE).

GAME = tetris
GAME_SRCS = src/tetris.c src/graphics.c src/input.c src/logsys.c
PORTLIB   = sdl2_lite

# game.mk needs the SoC build tree (variables.mak). Guarded so `make
# tetris-pc` still works in a checkout that never ran the SoC build.
BUILD_DIR ?= ../../soc/build/pocket
ifneq ($(wildcard $(BUILD_DIR)/software/include/generated/variables.mak),)
include ../game.mk

# Prepend the shadow-header dir so <SDL2/SDL.h> resolves to compat/SDL2/
# (the sdl2_lite shim), tyrian-style.
CFLAGS := -I$(CURDIR)/compat $(CFLAGS)
else
all:
	@echo "No SoC build tree at $(BUILD_DIR) — console build unavailable."
	@echo "PC twin: make tetris-pc"
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

tetris-pc: $(PC_GAME_SRCS) $(PC_HAL_SRC) ../sdl2_lite.h compat/SDL2/SDL.h
	@mkdir -p pcobj
	@for s in $(PC_GAME_SRCS); do \
	  gcc -Icompat -I.. -I../../soc/hal $(PC_FLAGS) \
	      -c $$s -o pcobj/$$(basename $$s .c).o || exit 1; done
	gcc $(PC_FLAGS) -I../../soc/hal $(shell pkg-config --cflags sdl2) \
	    -c $(PC_HAL_SRC) -o pcobj/hal_pc.o
	gcc -o $@ pcobj/*.o $(shell pkg-config --libs sdl2)
.PHONY: tetris-pc

pc-clean:
	rm -rf pcobj tetris-pc
.PHONY: pc-clean
