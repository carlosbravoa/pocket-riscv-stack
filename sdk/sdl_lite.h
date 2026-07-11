// sdl_lite — the SDL-1.2 subset real ports actually touch, over the HAL.
//
// Target: 8bpp palettized 320x200/320x240 games (OpenTyrian and its era).
// Same names and shapes as SDL 1.2 so a port's diff stays small; this is a
// shim, not SDL — anything not listed here doesn't exist.
//
//   SDL_Surface *s = SDL_SetVideoMode(320, 200, 8, 0);   // letterboxed
//   SDL_SetColors(s, pal, 0, 256);
//   ... draw into s->pixels (pitch == s->pitch) ...
//   SDL_Flip(s);                          // copy + page flip + audio pump
//
// Input arrives as SDL_KEYDOWN/SDL_KEYUP via SDL_PollEvent; the pad-to-key
// map defaults to OpenTyrian's bindings (arrows/space/enter/lctrl/lalt/esc)
// and can be replaced with SDL_lite_set_keymap().
//
// SPDX-License-Identifier: BSD-2-Clause
#ifndef RVSTACK_SDL_LITE_H
#define RVSTACK_SDL_LITE_H

#include <stdint.h>

typedef uint8_t  Uint8;
typedef uint16_t Uint16;
typedef uint32_t Uint32;
typedef int16_t  Sint16;
typedef int32_t  Sint32;

// ---------------------------------------------------------------- video
typedef struct { Uint8 r, g, b, unused; } SDL_Color;

typedef struct SDL_Surface {
	void *pixels;                   // 8bpp indexed
	int   w, h, pitch;
} SDL_Surface;

SDL_Surface *SDL_SetVideoMode(int w, int h, int bpp, Uint32 flags);
int  SDL_Flip(SDL_Surface *s);      // shadow -> backbuffer -> present (+pump)
void SDL_SetColors(SDL_Surface *s, const SDL_Color *colors, int first, int n);
SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int w, int h, int bpp,
                                  Uint32 rm, Uint32 gm, Uint32 bm, Uint32 am);
void SDL_FreeSurface(SDL_Surface *s);
typedef struct { Sint16 x, y; Uint16 w, h; } SDL_Rect;
int  SDL_BlitSurface(SDL_Surface *src, const SDL_Rect *sr,
                     SDL_Surface *dst, SDL_Rect *dr);
int  SDL_FillRect(SDL_Surface *dst, const SDL_Rect *r, Uint32 color);

// ---------------------------------------------------------------- events
typedef enum {                       // the keysyms OpenTyrian-era games use
	SDLK_UNKNOWN = 0,
	SDLK_UP, SDLK_DOWN, SDLK_LEFT, SDLK_RIGHT,
	SDLK_SPACE, SDLK_RETURN, SDLK_ESCAPE,
	SDLK_LCTRL, SDLK_LALT, SDLK_p, SDLK_s,
	SDLK_LAST
} SDLKey;

#define SDL_KEYDOWN 2
#define SDL_KEYUP   3
#define SDL_QUIT    12

typedef struct { Uint8 type; struct { SDLKey sym; } keysym; } SDL_KeyboardEvent;
typedef union {
	Uint8 type;
	SDL_KeyboardEvent key;
} SDL_Event;

int SDL_PollEvent(SDL_Event *ev);   // also runs input_poll() once per frame
Uint8 *SDL_GetKeyState(int *numkeys);

// pad-bit -> keysym map (16 entries, one per HAL_BTN bit; SDLK_UNKNOWN=unused)
void SDL_lite_set_keymap(const SDLKey map[16]);

// ---------------------------------------------------------------- time
Uint32 SDL_GetTicks(void);
void   SDL_Delay(Uint32 ms);

// ---------------------------------------------------------------- audio
typedef struct {
	int    freq;                    // shim always runs 48000
	Uint16 format;                  // accepted, assumed S16LSB
	Uint8  channels;                // 1 or 2
	Uint16 samples;                 // callback granularity (frames)
	void (*callback)(void *userdata, Uint8 *stream, int len);
	void  *userdata;
} SDL_AudioSpec;

int  SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained);
void SDL_PauseAudio(int pause_on);
void SDL_CloseAudio(void);
// The shim has no interrupts: the callback is drained inside SDL_Flip() (and
// SDL_Delay). Ports that spin without flipping should call this per frame:
void SDL_lite_audio_pump(void);

// ---------------------------------------------------------------- misc
int  SDL_Init(Uint32 flags);
void SDL_Quit(void);                // returns to the game picker (sys_exit)
#define SDL_INIT_VIDEO 0x20
#define SDL_INIT_AUDIO 0x10
#define SDL_INIT_TIMER 0x01

#endif // RVSTACK_SDL_LITE_H
