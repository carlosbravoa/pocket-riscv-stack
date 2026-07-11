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

/* PC twin, round 2: these SDL2-named functions are DEFINED by sdl2_shim.c.
 * Object symbols beat libSDL2's shared symbols at link time, so without a
 * rename hal_pc.c's REAL SDL2 calls bind to the shim (audio state clobber,
 * segv — found the hard way). Console builds don't define RVSTACK_PC. */
#ifdef RVSTACK_PC
#define SDL_BuildAudioCVT            RVSDL2_BuildAudioCVT
#define SDL_CloseAudioDevice         RVSDL2_CloseAudioDevice
#define SDL_ConvertAudio             RVSDL2_ConvertAudio
#define SDL_GetError                 RVSDL2_GetError
#define SDL_GetModState              RVSDL2_GetModState
#define SDL_GetNumVideoDisplays      RVSDL2_GetNumVideoDisplays
#define SDL_GetPixelFormatName       RVSDL2_GetPixelFormatName
#define SDL_GetScancodeFromName      RVSDL2_GetScancodeFromName
#define SDL_GetScancodeName          RVSDL2_GetScancodeName
#define SDL_InitSubSystem            RVSDL2_InitSubSystem
#define SDL_LockAudioDevice          RVSDL2_LockAudioDevice
#define SDL_MapRGB                   RVSDL2_MapRGB
#define SDL_OpenAudioDevice          RVSDL2_OpenAudioDevice
#define SDL_PauseAudioDevice         RVSDL2_PauseAudioDevice
#define SDL_QuitSubSystem            RVSDL2_QuitSubSystem
#define SDL_SetHint                  RVSDL2_SetHint
#define SDL_SetRelativeMouseMode     RVSDL2_SetRelativeMouseMode
#define SDL_ShowCursor               RVSDL2_ShowCursor
#define SDL_UnlockAudioDevice        RVSDL2_UnlockAudioDevice
#define SDL_WasInit                  RVSDL2_WasInit
#define SDL_strlcpy                  RVSDL2_strlcpy
#endif

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
