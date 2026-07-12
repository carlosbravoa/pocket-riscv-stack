// sdl2_lite — the SDL2 subset small ports actually touch, over the HAL.
//
// Sibling of sdl_lite (the SDL-1.2 shim): where sdl_lite serves 8bpp
// palettized games that already think in indexed surfaces, sdl2_lite serves
// games written against SDL2's Window/Renderer/Texture model. Same names and
// shapes as SDL2 so a port's diff stays small; this is a shim, not SDL —
// anything not listed here doesn't exist.
//
//   SDL_Window *w; SDL_Renderer *r;
//   SDL_CreateWindowAndRenderer(220, 240, 0, &w, &r);   // must fit 320x240
//   SDL_SetRenderDrawColor(r, 255, 0, 0, 255);          // quantized (below)
//   SDL_RenderFillRect(r, &rect);
//   SDL_RenderPresent(r);                               // copy + vsync flip
//
// THE OPINIONATED CHOICE — color quantization: the console framebuffer is
// 8-bit palettized (hal.h), SDL2 renders RGBA. The shim builds the 256-entry
// hardware palette FROM THE COLORS THE GAME DRAWS: every distinct RGB seen in
// SDL_SetRenderDrawColor / TTF_Render* gets a palette slot on first use
// (entry 0 is reserved as the transparent/black backdrop, so 255 usable).
// Games of the class this targets (Tetris & friends: flat fills, small fixed
// color sets) never notice. A game drawing >255 distinct colors gets
// nearest-match on the overflow — port such a game against sdl_lite's
// indexed surfaces instead.
//
// Input arrives as SDL2 keyboard state/events synthesized from the gamepad;
// the pad-bit -> scancode map defaults to a retro-puzzle layout (arrows,
// A=X-key rotate, B=Z-key rotate-CCW, START=Return, SELECT=Escape) and can
// be replaced with RVSDL2_SetPadMap(). SELECT+START posts SDL_QUIT.
//
// LINK NAMESPACE (the tyrian lesson, see sdk/tyrian/compat/SDL_stdinc.h):
// every public symbol is RVSDL2_-prefixed via the macros below — a shim
// symbol named like a real SDL function hijacks desktop libSDL2 when the PC
// twin links (hal_pc.c uses the real thing). Unlike sdl_lite's RVL_ renames
// this is UNCONDITIONAL (console too): nothing else provides SDL2 names on
// the console, and one namespace story beats two.
//
// Covered API (grown from the sdl2-tetris port — extend as ports demand):
//   init/quit/error, CreateWindowAndRenderer (+ title/destroy no-ops),
//   SetRenderDrawColor/GetRenderDrawColor, RenderClear, RenderFillRect,
//   RenderCopy (unscaled), RenderPresent, CreateTextureFromSurface,
//   QueryTexture, DestroyTexture, FreeSurface, PollEvent (key + quit),
//   GetKeyboardState, GetTicks, Delay, and an SDL2_ttf corner (below).
// NOT here (deliberately, until a port needs them): audio (no SDL2 port has
//   used it yet — use hal.h pcm_play/audio_stream_* or sdl_lite's callback
//   model), RenderCopy scaling, SDL_CreateTexture/LockTexture streaming,
//   RenderDrawLine/Point, joysticks-as-joysticks, mouse, multiple windows,
//   SDL_image, real TTF rasterization.
//
// SDL2_ttf subset: TTF_OpenFont accepts (and ignores) the font path — text
// renders with the SDK's built-in public-domain 8x8 bitmap font
// (sdk/font8x8_basic.h), ASCII only, 8px tall regardless of ptsize.
// TTF_RenderUTF8_Blended returns an 8bpp surface (0 = transparent) ready for
// SDL_CreateTextureFromSurface. Enough for HUDs and menus; a game that needs
// real glyphs should pre-render them into its pak.
//
// SPDX-License-Identifier: BSD-2-Clause
#ifndef RVSTACK_SDL2_LITE_H
#define RVSTACK_SDL2_LITE_H

#include <stdint.h>
#include <stddef.h>

// ------------------------------------------------------- RVSDL2_ renames
// Object symbols beat shared-library symbols at link time; without these the
// PC twin's REAL SDL2 calls (hal_pc.c) would bind to the shim. All public
// names, no exceptions.
#define SDL_Init                     RVSDL2_Init
#define SDL_Quit                     RVSDL2_Quit
#define SDL_GetError                 RVSDL2_GetError
#define SDL_CreateWindowAndRenderer  RVSDL2_CreateWindowAndRenderer
#define SDL_SetWindowTitle           RVSDL2_SetWindowTitle
#define SDL_DestroyWindow            RVSDL2_DestroyWindow
#define SDL_DestroyRenderer          RVSDL2_DestroyRenderer
#define SDL_SetRenderDrawColor       RVSDL2_SetRenderDrawColor
#define SDL_GetRenderDrawColor       RVSDL2_GetRenderDrawColor
#define SDL_RenderClear              RVSDL2_RenderClear
#define SDL_RenderFillRect           RVSDL2_RenderFillRect
#define SDL_RenderCopy               RVSDL2_RenderCopy
#define SDL_RenderPresent            RVSDL2_RenderPresent
#define SDL_CreateTextureFromSurface RVSDL2_CreateTextureFromSurface
#define SDL_QueryTexture             RVSDL2_QueryTexture
#define SDL_DestroyTexture           RVSDL2_DestroyTexture
#define SDL_FreeSurface              RVSDL2_FreeSurface
#define SDL_PollEvent                RVSDL2_PollEvent
#define SDL_GetKeyboardState         RVSDL2_GetKeyboardState
#define SDL_GetTicks                 RVSDL2_GetTicks
#define SDL_Delay                    RVSDL2_Delay
#define TTF_Init                     RVSDL2_TTF_Init
#define TTF_Quit                     RVSDL2_TTF_Quit
#define TTF_GetError                 RVSDL2_TTF_GetError
#define TTF_OpenFont                 RVSDL2_TTF_OpenFont
#define TTF_CloseFont                RVSDL2_TTF_CloseFont
#define TTF_RenderUTF8_Blended       RVSDL2_TTF_RenderUTF8_Blended

typedef uint8_t  Uint8;
typedef int8_t   Sint8;
typedef uint16_t Uint16;
typedef int16_t  Sint16;
typedef uint32_t Uint32;
typedef int32_t  Sint32;

// ------------------------------------------------------------------ init

#define SDL_INIT_TIMER    0x00000001u
#define SDL_INIT_AUDIO    0x00000010u
#define SDL_INIT_VIDEO    0x00000020u
#define SDL_INIT_JOYSTICK 0x00000200u

int  SDL_Init(Uint32 flags);            // runs sys_init(); returns 0
void SDL_Quit(void);                    // exits to the game picker (sys_exit)
const char *SDL_GetError(void);         // always "" (nothing here soft-fails)

// ----------------------------------------------------------------- video

typedef struct { int x, y, w, h; } SDL_Rect;              // SDL2 shape (int)
typedef struct { Uint8 r, g, b, a; } SDL_Color;

// 8bpp indexed, byte 0 = transparent (palette entry 0 is reserved).
typedef struct SDL_Surface {
	int    w, h, pitch;
	Uint8 *pixels;
} SDL_Surface;

typedef struct SDL_Window   SDL_Window;    // opaque singletons: one screen,
typedef struct SDL_Renderer SDL_Renderer;  // one renderer, that's the console
typedef struct SDL_Texture  SDL_Texture;

// w x h must fit the 320x240 panel; the canvas is centered (letterboxed).
// Returns 0, or -1 if the mode doesn't fit — patch the game's layout, don't
// scale (there is no scaler).
int  SDL_CreateWindowAndRenderer(int w, int h, Uint32 flags,
                                 SDL_Window **win, SDL_Renderer **ren);
void SDL_SetWindowTitle(SDL_Window *w, const char *title);   // no-op
void SDL_DestroyWindow(SDL_Window *w);                       // no-op
void SDL_DestroyRenderer(SDL_Renderer *r);                   // no-op

int  SDL_SetRenderDrawColor(SDL_Renderer *r, Uint8 rd, Uint8 g, Uint8 b, Uint8 a);
int  SDL_GetRenderDrawColor(SDL_Renderer *r, Uint8 *rd, Uint8 *g, Uint8 *b, Uint8 *a);
int  SDL_RenderClear(SDL_Renderer *r);
int  SDL_RenderFillRect(SDL_Renderer *r, const SDL_Rect *rect);  // NULL = all
// Unscaled blit of the texture (or srcrect cut) at dstrect->x/y; texture
// byte 0 is transparent. dstrect w/h are IGNORED (no scaler — honest).
int  SDL_RenderCopy(SDL_Renderer *r, SDL_Texture *t,
                    const SDL_Rect *srcrect, const SDL_Rect *dstrect);
// Copy canvas -> HAL back buffer, reload palette if it grew, vsync flip.
void SDL_RenderPresent(SDL_Renderer *r);

SDL_Texture *SDL_CreateTextureFromSurface(SDL_Renderer *r, SDL_Surface *s);
int  SDL_QueryTexture(SDL_Texture *t, Uint32 *format, int *access,
                      int *w, int *h);
void SDL_DestroyTexture(SDL_Texture *t);
void SDL_FreeSurface(SDL_Surface *s);

// ---------------------------------------------------------------- events
// Real SDL2 scancode values (subset); keystate array is indexed by these.

typedef enum {
	SDL_SCANCODE_UNKNOWN = 0,
	SDL_SCANCODE_A = 4,  SDL_SCANCODE_B = 5,  SDL_SCANCODE_C = 6,
	SDL_SCANCODE_D = 7,  SDL_SCANCODE_E = 8,  SDL_SCANCODE_F = 9,
	SDL_SCANCODE_G = 10, SDL_SCANCODE_H = 11, SDL_SCANCODE_I = 12,
	SDL_SCANCODE_J = 13, SDL_SCANCODE_K = 14, SDL_SCANCODE_L = 15,
	SDL_SCANCODE_M = 16, SDL_SCANCODE_N = 17, SDL_SCANCODE_O = 18,
	SDL_SCANCODE_P = 19, SDL_SCANCODE_Q = 20, SDL_SCANCODE_R = 21,
	SDL_SCANCODE_S = 22, SDL_SCANCODE_T = 23, SDL_SCANCODE_U = 24,
	SDL_SCANCODE_V = 25, SDL_SCANCODE_W = 26, SDL_SCANCODE_X = 27,
	SDL_SCANCODE_Y = 28, SDL_SCANCODE_Z = 29,
	SDL_SCANCODE_1 = 30, SDL_SCANCODE_2 = 31, SDL_SCANCODE_3 = 32,
	SDL_SCANCODE_4 = 33, SDL_SCANCODE_5 = 34, SDL_SCANCODE_6 = 35,
	SDL_SCANCODE_7 = 36, SDL_SCANCODE_8 = 37, SDL_SCANCODE_9 = 38,
	SDL_SCANCODE_0 = 39,
	SDL_SCANCODE_RETURN = 40, SDL_SCANCODE_ESCAPE = 41,
	SDL_SCANCODE_BACKSPACE = 42, SDL_SCANCODE_TAB = 43,
	SDL_SCANCODE_SPACE = 44,
	SDL_SCANCODE_RIGHT = 79, SDL_SCANCODE_LEFT = 80,
	SDL_SCANCODE_DOWN = 81, SDL_SCANCODE_UP = 82,
	SDL_SCANCODE_LCTRL = 224, SDL_SCANCODE_LSHIFT = 225,
	SDL_SCANCODE_LALT = 226,
	SDL_SCANCODE_RCTRL = 228, SDL_SCANCODE_RSHIFT = 229,
	SDL_SCANCODE_RALT = 230,
	SDL_NUM_SCANCODES = 512
} SDL_Scancode;

#define SDL_QUIT    0x100
#define SDL_KEYDOWN 0x300
#define SDL_KEYUP   0x301

#define SDL_RELEASED 0
#define SDL_PRESSED  1

typedef struct SDL_Keysym {
	SDL_Scancode scancode;
	Sint32       sym;                   // mirrors scancode (no keymap layer)
	Uint16       mod;                   // always 0 (pads have no modifiers)
	Uint32       unused;
} SDL_Keysym;

typedef struct { Uint32 type; Uint8 state; Uint8 repeat; SDL_Keysym keysym; }
	SDL_KeyboardEvent;

typedef union SDL_Event {
	Uint32            type;
	SDL_KeyboardEvent key;
	Uint8             padding[32];
} SDL_Event;

// Pad edges become key events; also runs input_poll() (time-gated, so
// spin-polling loops still see fresh pads) and the deferred-flip poll.
int SDL_PollEvent(SDL_Event *ev);
// Scancode-indexed held-key array (SDL_NUM_SCANCODES entries), updated by
// SDL_PollEvent — drain events first, exactly like real SDL2.
const Uint8 *SDL_GetKeyboardState(int *numkeys);

// RVSTACK extension: pad-bit -> scancode map (16 entries, one per HAL_BTN
// bit; SDL_SCANCODE_UNKNOWN = unused). Call before/after window creation.
// Default: dpad=arrows, A=X, B=Z, X=Space, Y/L1=LShift, R1=Space,
// SELECT=Escape, START=Return.
void RVSDL2_SetPadMap(const Uint16 map[16]);

// ------------------------------------------------------------------ time

Uint32 SDL_GetTicks(void);
void   SDL_Delay(Uint32 ms);            // busy-waits; keeps deferred flips live

// --------------------------------------------------- SDL2_ttf (bitmap font)

typedef struct TTF_Font_s TTF_Font;

int         TTF_Init(void);             // 0
void        TTF_Quit(void);
const char *TTF_GetError(void);         // ""
// path ignored (built-in 8x8 font); ptsize accepted, glyphs stay 8px.
TTF_Font   *TTF_OpenFont(const char *file, int ptsize);
void        TTF_CloseFont(TTF_Font *f);
// ASCII only (high bytes render as blank); 0 = transparent in the result.
SDL_Surface *TTF_RenderUTF8_Blended(TTF_Font *f, const char *text,
                                    SDL_Color color);

#endif // RVSTACK_SDL2_LITE_H
