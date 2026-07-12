/*
 * SDL.h — riscv-stack compat shim for the Wolf4SDL port.
 *
 * Wolf4SDL v2.0 is written against SDL2. The riscv-stack SDK provides
 * sdl_lite (an SDL-1.2-subset shim over the HAL); this header re-creates
 * the SDL2 names the game actually uses on top of it — same architecture
 * as the OpenTyrian2000 port (sdk/tyrian/compat/SDL.h), with one twist:
 *
 *  - Surfaces: Wolf pokes surface->format->palette (VL_SetPalette), which
 *    sdl_lite's 4-member SDL_Surface doesn't have. So the surface API here
 *    does NOT bind to sdl_lite.o; compat/wsdl.c implements richer surfaces
 *    on plain malloc memory under wsdl_-prefixed link names (see below).
 *    Only the PRESENT path touches sdl_lite (compat/lite_bridge.c).
 *  - Events: SDL2-shaped key events are synthesized from the pad by
 *    lite_bridge.c and queued/translated in wsdl.c.
 *  - Audio: none here — the sound manager (compat/id_sd_rv.c) talks to the
 *    bridge directly (callback audio + the HAL's real OPL3).
 *  - Time/init: SDL_GetTicks/SDL_Delay/SDL_Init/SDL_Quit resolve straight
 *    to sdl_lite. SDL_Quit exits to the game picker.
 *
 * THE LINK-NAMESPACE TRAP (cost the Tyrian port a segfault): any symbol we
 * define that is named like a real SDL function hijacks desktop libSDL2 on
 * the PC twin (hal_pc.c uses the real SDL2). Everything implemented in
 * compat/ is therefore #define-renamed to a wsdl_/WSDL_ prefix for BOTH
 * targets (on the console this also dodges sdl_lite's own SDL_* names);
 * the few calls that intentionally resolve to sdl_lite follow ITS rename
 * scheme (RVL_ on PC — see sdk/sdl_lite.h).
 *
 * This file is part of the Wolf4SDL riscv-stack port glue; like the game
 * sources it accompanies, it inherits Wolf4SDL's license (see
 * src/license-id.txt). The SDK it talks to stays BSD-2-Clause.
 */
#ifndef RVSTACK_WOLF_SDL2_COMPAT_H
#define RVSTACK_WOLF_SDL2_COMPAT_H

#include <stdint.h>
#include <stddef.h>

/* ----- calls that resolve to sdl_lite.o (follow its PC rename) ---------- */
#ifdef RVSTACK_PC
#define SDL_GetTicks         RVL_GetTicks
#define SDL_Delay            RVL_Delay
#define SDL_Init             RVL_Init
#define SDL_Quit             RVL_Quit
#endif

/* ----- everything implemented in compat/wsdl.c: rename ALWAYS ----------- */
#define SDL_CreateRGBSurface  wsdl_CreateRGBSurface
#define SDL_FreeSurface       wsdl_FreeSurface
#define SDL_FillRect          wsdl_FillRect
#define SDL_BlitSurface       wsdl_BlitSurface
#define SDL_SetPaletteColors  wsdl_SetPaletteColors
#define SDL_LockSurface       wsdl_LockSurface
#define SDL_UnlockSurface     wsdl_UnlockSurface
#define SDL_MapRGBA           wsdl_MapRGBA
#define SDL_SaveBMP           wsdl_SaveBMP
#define SDL_PollEvent         wsdl_PollEvent
#define SDL_WaitEvent         wsdl_WaitEvent
#define SDL_PushEvent         wsdl_PushEvent
#define SDL_EventState        wsdl_EventState
#define SDL_GetError          wsdl_GetError
#define SDL_GetModState       wsdl_GetModState
#define SDL_GetMouseState     wsdl_GetMouseState
#define SDL_GetRelativeMouseState wsdl_GetRelativeMouseState
#define SDL_ShowCursor        wsdl_ShowCursor
#define SDL_SetRelativeMouseMode wsdl_SetRelativeMouseMode
#define SDL_SetWindowGrab     wsdl_SetWindowGrab
#define SDL_WarpMouseInWindow wsdl_WarpMouseInWindow
#define SDL_ShowSimpleMessageBox wsdl_ShowSimpleMessageBox
#define SDL_SetHint           wsdl_SetHint

typedef uint8_t  Uint8;
typedef int8_t   Sint8;
typedef uint16_t Uint16;
typedef int16_t  Sint16;
typedef uint32_t Uint32;
typedef int32_t  Sint32;
typedef uint64_t Uint64;
typedef int64_t  Sint64;
typedef int      SDL_bool;
#define SDL_FALSE 0
#define SDL_TRUE  1

/* ------------------------------------------------------------------ init */

#define SDL_INIT_TIMER    0x00000001u
#define SDL_INIT_AUDIO    0x00000010u
#define SDL_INIT_VIDEO    0x00000020u
#define SDL_INIT_JOYSTICK 0x00000200u

int  SDL_Init(Uint32 flags);            /* -> sdl_lite (returns 0) */
void SDL_Quit(void);                    /* -> sdl_lite (sys_exit: game picker) */

const char *SDL_GetError(void);
int SDL_SetHint(const char *name, const char *value);
#define SDL_HINT_RENDER_SCALE_QUALITY "SDL_RENDER_SCALE_QUALITY"

/* ------------------------------------------------------------------ video */

#define SDL_ALPHA_OPAQUE 255

typedef struct SDL_Color { Uint8 r, g, b, a; } SDL_Color;

typedef struct SDL_Palette {
	int        ncolors;
	SDL_Color *colors;
} SDL_Palette;

typedef struct SDL_PixelFormat {
	Uint32       format;
	SDL_Palette *palette;
	Uint8        BitsPerPixel;
	Uint8        BytesPerPixel;
} SDL_PixelFormat;

typedef struct SDL_Rect { int x, y, w, h; } SDL_Rect;

typedef struct SDL_Surface {
	void            *pixels;            /* 8bpp indexed */
	int              w, h, pitch;
	SDL_PixelFormat *format;            /* format->palette is real (256) */
} SDL_Surface;

SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int w, int h, int bpp,
                                  Uint32 rm, Uint32 gm, Uint32 bm, Uint32 am);
void SDL_FreeSurface(SDL_Surface *s);
int  SDL_FillRect(SDL_Surface *dst, const SDL_Rect *r, Uint32 color);
int  SDL_BlitSurface(SDL_Surface *src, const SDL_Rect *sr,
                     SDL_Surface *dst, SDL_Rect *dr);
int  SDL_SetPaletteColors(SDL_Palette *pal, const SDL_Color *colors,
                          int firstcolor, int ncolors);
int  SDL_LockSurface(SDL_Surface *s);
void SDL_UnlockSurface(SDL_Surface *s);
Uint32 SDL_MapRGBA(const SDL_PixelFormat *f, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
int  SDL_SaveBMP(SDL_Surface *s, const char *file);

#define SDL_MUSTLOCK(s) 0

/* window/renderer/texture: opaque, never created (VL_SetVGAPlaneMode is
 * patched to present through the bridge instead) */
typedef struct SDL_Window   SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture  SDL_Texture;

#define SDL_PIXELFORMAT_ARGB8888 0x16362004u

int SDL_ShowCursor(int toggle);
#define SDL_DISABLE 0
#define SDL_ENABLE  1
int SDL_SetRelativeMouseMode(SDL_bool enabled);
void SDL_SetWindowGrab(SDL_Window *w, SDL_bool grabbed);
void SDL_WarpMouseInWindow(SDL_Window *w, int x, int y);

#define SDL_MESSAGEBOX_ERROR       0x10
#define SDL_MESSAGEBOX_INFORMATION 0x40
int SDL_ShowSimpleMessageBox(Uint32 flags, const char *title,
                             const char *message, SDL_Window *w);

/* riscv-stack extensions (implemented in compat/wsdl.c / lite_bridge.c) */
void RVSDL_InitVideo(void);
void RVSDL_PresentIndexed(const void *pixels, int pitch, int w, int h,
                          const void *colors256);
void rvb_progress(int stage);           /* load-progress beacon */

/* -------------------------------------------------------------- keyboard */

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
	SDL_SCANCODE_SPACE = 44, SDL_SCANCODE_MINUS = 45,
	SDL_SCANCODE_EQUALS = 46, SDL_SCANCODE_LEFTBRACKET = 47,
	SDL_SCANCODE_RIGHTBRACKET = 48, SDL_SCANCODE_BACKSLASH = 49,
	SDL_SCANCODE_SEMICOLON = 51, SDL_SCANCODE_APOSTROPHE = 52,
	SDL_SCANCODE_GRAVE = 53, SDL_SCANCODE_COMMA = 54,
	SDL_SCANCODE_PERIOD = 55, SDL_SCANCODE_SLASH = 56,
	SDL_SCANCODE_CAPSLOCK = 57,
	SDL_SCANCODE_F1 = 58, SDL_SCANCODE_F2 = 59, SDL_SCANCODE_F3 = 60,
	SDL_SCANCODE_F4 = 61, SDL_SCANCODE_F5 = 62, SDL_SCANCODE_F6 = 63,
	SDL_SCANCODE_F7 = 64, SDL_SCANCODE_F8 = 65, SDL_SCANCODE_F9 = 66,
	SDL_SCANCODE_F10 = 67, SDL_SCANCODE_F11 = 68, SDL_SCANCODE_F12 = 69,
	SDL_SCANCODE_SCROLLLOCK = 71, SDL_SCANCODE_PAUSE = 72,
	SDL_SCANCODE_INSERT = 73, SDL_SCANCODE_HOME = 74,
	SDL_SCANCODE_PAGEUP = 75, SDL_SCANCODE_DELETE = 76,
	SDL_SCANCODE_END = 77, SDL_SCANCODE_PAGEDOWN = 78,
	SDL_SCANCODE_RIGHT = 79, SDL_SCANCODE_LEFT = 80,
	SDL_SCANCODE_DOWN = 81, SDL_SCANCODE_UP = 82,
	SDL_SCANCODE_NUMLOCKCLEAR = 83,
	SDL_SCANCODE_KP_DIVIDE = 84, SDL_SCANCODE_KP_MULTIPLY = 85,
	SDL_SCANCODE_KP_MINUS = 86, SDL_SCANCODE_KP_PLUS = 87,
	SDL_SCANCODE_KP_ENTER = 88,
	SDL_SCANCODE_KP_1 = 89, SDL_SCANCODE_KP_2 = 90, SDL_SCANCODE_KP_3 = 91,
	SDL_SCANCODE_KP_4 = 92, SDL_SCANCODE_KP_5 = 93, SDL_SCANCODE_KP_6 = 94,
	SDL_SCANCODE_KP_7 = 95, SDL_SCANCODE_KP_8 = 96, SDL_SCANCODE_KP_9 = 97,
	SDL_SCANCODE_KP_0 = 98,
	SDL_SCANCODE_LCTRL = 224, SDL_SCANCODE_LSHIFT = 225,
	SDL_SCANCODE_LALT = 226, SDL_SCANCODE_LGUI = 227,
	SDL_SCANCODE_RCTRL = 228, SDL_SCANCODE_RSHIFT = 229,
	SDL_SCANCODE_RALT = 230, SDL_SCANCODE_RGUI = 231,
	SDL_NUM_SCANCODES = 512
} SDL_Scancode;

typedef enum {
	KMOD_NONE   = 0x0000,
	KMOD_LSHIFT = 0x0001, KMOD_RSHIFT = 0x0002,
	KMOD_LCTRL  = 0x0040, KMOD_RCTRL  = 0x0080,
	KMOD_LALT   = 0x0100, KMOD_RALT   = 0x0200,
	KMOD_NUM    = 0x1000,
	KMOD_CTRL   = KMOD_LCTRL | KMOD_RCTRL,
	KMOD_SHIFT  = KMOD_LSHIFT | KMOD_RSHIFT,
	KMOD_ALT    = KMOD_LALT | KMOD_RALT
} SDL_Keymod;

typedef struct SDL_Keysym {
	SDL_Scancode scancode;
	Sint32       sym;
	Uint16       mod;
	Uint32       unused;
} SDL_Keysym;

SDL_Keymod SDL_GetModState(void);

/* -------------------------------------------------------------- events */

#define SDL_QUIT            0x100
#define SDL_KEYDOWN         0x300
#define SDL_KEYUP           0x301
#define SDL_TEXTINPUT       0x303
#define SDL_MOUSEMOTION     0x400
#define SDL_JOYBUTTONDOWN   0x603
#define SDL_JOYBUTTONUP     0x604

#define SDL_RELEASED 0
#define SDL_PRESSED  1
#define SDL_IGNORE   0

#define SDL_TEXTINPUTEVENT_TEXT_SIZE 32

typedef struct { Uint32 type; Uint8 state; SDL_Keysym keysym; } SDL_KeyboardEvent;
typedef struct { Uint32 type; Uint8 button; } SDL_JoyButtonEvent;
typedef struct { Uint32 type; char text[SDL_TEXTINPUTEVENT_TEXT_SIZE]; } SDL_TextInputEvent;

typedef union SDL_Event {
	Uint32             type;
	SDL_KeyboardEvent  key;
	SDL_JoyButtonEvent jbutton;
	SDL_TextInputEvent text;
} SDL_Event;

int SDL_PollEvent(SDL_Event *ev);
int SDL_WaitEvent(SDL_Event *ev);
int SDL_PushEvent(const SDL_Event *ev);
Uint8 SDL_EventState(Uint32 type, int state);

/* -------------------------------------------------------------- time */

Uint32 SDL_GetTicks(void);              /* -> sdl_lite */
void   SDL_Delay(Uint32 ms);            /* -> sdl_lite (pumps audio) */

/* -------------------------------------------------------------- mouse */

#define SDL_BUTTON(x)     (1u << ((x) - 1))
#define SDL_BUTTON_LEFT   1
#define SDL_BUTTON_MIDDLE 2
#define SDL_BUTTON_RIGHT  3

Uint32 SDL_GetMouseState(int *x, int *y);
Uint32 SDL_GetRelativeMouseState(int *x, int *y);

/* ------------------------------------------------------------ joystick */

typedef struct SDL_Joystick SDL_Joystick;

#define SDL_HAT_CENTERED 0x00
#define SDL_HAT_UP       0x01
#define SDL_HAT_RIGHT    0x02
#define SDL_HAT_DOWN     0x04
#define SDL_HAT_LEFT     0x08

static inline int SDL_NumJoysticks(void) { return 0; }
static inline SDL_Joystick *SDL_JoystickOpen(int i) { (void)i; return (SDL_Joystick *)0; }
static inline void SDL_JoystickClose(SDL_Joystick *j) { (void)j; }
static inline int SDL_JoystickNumButtons(SDL_Joystick *j) { (void)j; return 0; }
static inline int SDL_JoystickNumHats(SDL_Joystick *j) { (void)j; return 0; }
static inline Sint16 SDL_JoystickGetAxis(SDL_Joystick *j, int a) { (void)j; (void)a; return 0; }
static inline Uint8 SDL_JoystickGetButton(SDL_Joystick *j, int b) { (void)j; (void)b; return 0; }
static inline Uint8 SDL_JoystickGetHat(SDL_Joystick *j, int h) { (void)j; (void)h; return SDL_HAT_CENTERED; }
static inline void SDL_JoystickUpdate(void) {}

#endif /* RVSTACK_WOLF_SDL2_COMPAT_H */
