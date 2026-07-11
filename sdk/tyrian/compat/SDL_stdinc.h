/*
 * SDL_stdinc.h — riscv-stack compat shim for the OpenTyrian2000 port.
 *
 * Part of sdk/tyrian's SDL2-flavored compatibility layer over the SDK's
 * sdl_lite (see compat/SDL.h for the full story). Types + small stdlib-ish
 * helpers only.
 *
 * This file is part of the OpenTyrian2000 riscv-stack port glue and is
 * licensed GPL-2.0-or-later (it inherits the game's license; the SDK it
 * talks to stays BSD).
 */
#ifndef RVSTACK_SDL_STDINC_H
#define RVSTACK_SDL_STDINC_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef uint8_t  Uint8;
typedef int8_t   Sint8;
typedef uint16_t Uint16;
typedef int16_t  Sint16;
typedef uint32_t Uint32;
typedef int32_t  Sint32;
typedef uint64_t Uint64;
typedef int64_t  Sint64;

typedef enum { SDL_FALSE = 0, SDL_TRUE = 1 } SDL_bool;

size_t SDL_strlcpy(char *dst, const char *src, size_t maxlen);

#define SDL_min(a, b) ((a) < (b) ? (a) : (b))
#define SDL_max(a, b) ((a) > (b) ? (a) : (b))

#endif /* RVSTACK_SDL_STDINC_H */
