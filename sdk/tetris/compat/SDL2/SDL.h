/*
 * SDL2/SDL.h — shadow header for the sdl2-tetris riscv-stack port.
 *
 * The vendored sources include <SDL2/SDL.h>; with -Icompat first on the
 * include path they get the SDK's sdl2_lite shim instead of desktop SDL2
 * (whose link namespace belongs to the PC twin's hal_pc.c — see the
 * RVSDL2_ rename block in sdk/sdl2_lite.h).
 *
 * Real SDL.h drags in most of libc; the vendored sources lean on that for
 * strcmp/rand, so provide the same courtesy here.
 *
 * MIT (port glue; inherits the game's license, see ../../LICENSE).
 */
#ifndef RVSTACK_TETRIS_SDL2_SHADOW_H
#define RVSTACK_TETRIS_SDL2_SHADOW_H

#include "sdl2_lite.h"
#include <string.h>
#include <stdlib.h>

#endif
