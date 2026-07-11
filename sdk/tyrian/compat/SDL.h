/*
 * SDL.h — riscv-stack compat shim for the OpenTyrian2000 port.
 *
 * The game is written against SDL2. The riscv-stack SDK provides sdl_lite
 * (an SDL-1.2-subset shim over the HAL: 320x200x8 surface + Flip + pad key
 * events + GetTicks/Delay + callback audio). This header re-creates the
 * SDL2 names the game actually uses on top of that:
 *
 *  - Surfaces: compat SDL_Surface has the EXACT layout of sdl_lite's
 *    SDL_Surface {pixels,w,h,pitch}, so SDL_CreateRGBSurface / SDL_FreeSurface
 *    / SDL_FillRect resolve straight to sdl_lite.o at link time. The game
 *    only dereferences ->format inside assert()s (compiled out, -DNDEBUG)
 *    and in video_scale.c (not built), so the member is omitted.
 *  - Events: SDL2's richer SDL_Event union differs from sdl_lite's, so
 *    SDL_PollEvent/SDL_PushEvent are macro-renamed to shim functions
 *    (compat/sdl2_shim.c) that translate lite key events (via
 *    compat/lite_bridge.c) into SDL2-shaped ones.
 *  - Audio: SDL_OpenAudioDevice maps onto sdl_lite's SDL_OpenAudio through
 *    the bridge; Lock/Unlock are no-ops (the callback runs synchronously
 *    from SDL_Flip/SDL_Delay — there is no audio thread to lock against).
 *  - Time/init: SDL_GetTicks/SDL_Delay/SDL_Init/SDL_Quit have identical
 *    signatures in sdl_lite and resolve to it directly. SDL_Quit exits to
 *    the game picker (sys_exit) — matching the game's fatal-error paths.
 *  - Joysticks/mouse/window: stubbed (pad-only hardware, one fixed screen).
 *
 * This file is part of the OpenTyrian2000 riscv-stack port glue and is
 * licensed GPL-2.0-or-later (it inherits the game's license; the SDK it
 * talks to stays BSD-2-Clause).
 */
#ifndef RVSTACK_SDL2_COMPAT_H
#define RVSTACK_SDL2_COMPAT_H

/* PC twin: sdl_lite's exports are RVL_-prefixed there so REAL SDL2 (used by
 * hal_pc.c) keeps the SDL_* link namespace. The game must follow the rename
 * for every symbol this header resolves onto sdl_lite.o — otherwise those
 * calls silently bind to libSDL2 and return REAL (layout-different!)
 * SDL_Surfaces. Cost us a segfault on first light. */
#ifdef RVSTACK_PC
#define SDL_CreateRGBSurface RVL_CreateRGBSurface
#define SDL_FreeSurface      RVL_FreeSurface
#define SDL_FillRect         RVL_FillRect
#define SDL_BlitSurface      RVL_BlitSurface
#define SDL_GetTicks         RVL_GetTicks
#define SDL_Delay            RVL_Delay
#define SDL_Init             RVL_Init
#define SDL_Quit             RVL_Quit
#endif

#include "SDL_stdinc.h"
#include "SDL_endian.h"

/* ------------------------------------------------------------------ init */

#define SDL_INIT_TIMER    0x00000001u
#define SDL_INIT_AUDIO    0x00000010u
#define SDL_INIT_VIDEO    0x00000020u
#define SDL_INIT_JOYSTICK 0x00000200u

int  SDL_Init(Uint32 flags);            /* -> sdl_lite (returns 0) */
void SDL_Quit(void);                    /* -> sdl_lite (sys_exit: game picker) */

int    SDL_InitSubSystem(Uint32 flags); /* shim: tracks a bitmask, returns 0 */
void   SDL_QuitSubSystem(Uint32 flags);
Uint32 SDL_WasInit(Uint32 flags);

const char *SDL_GetError(void);
int SDL_SetHint(const char *name, const char *value);
#define SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE "SDL_MOUSE_RELATIVE_SYSTEM_SCALE"

/* SDL_VERSION_ATLEAST(2,0,9) gates optional niceties; report an old SDL2. */
#define SDL_VERSION_ATLEAST(X, Y, Z) \
	(((X) < 2) || ((X) == 2 && (Y) == 0 && (Z) <= 0))

/* ------------------------------------------------------------------ video */

typedef struct { Uint8 r, g, b, a; } SDL_Color;

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

/* EXACT layout of sdl_lite's SDL_Surface — do not add members. */
typedef struct SDL_Surface {
	void *pixels;                       /* 8bpp indexed */
	int   w, h, pitch;
} SDL_Surface;

/* SDL 1.2-style rect (Sint16/Uint16): matches sdl_lite's SDL_FillRect. */
typedef struct { Sint16 x, y; Uint16 w, h; } SDL_Rect;

SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int w, int h, int bpp,
                                  Uint32 rm, Uint32 gm, Uint32 bm, Uint32 am);
void SDL_FreeSurface(SDL_Surface *s);
int  SDL_FillRect(SDL_Surface *dst, const SDL_Rect *r, Uint32 color);

#define SDL_MUSTLOCK(s) 0

typedef struct SDL_Window   SDL_Window;   /* opaque, never created */
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture  SDL_Texture;

#define SDL_PIXELFORMAT_RGB888 0x16161804u
#define SDL_PIXELFORMAT_RGB565 0x15151002u

Uint32 SDL_MapRGB(const SDL_PixelFormat *f, Uint8 r, Uint8 g, Uint8 b);
const char *SDL_GetPixelFormatName(Uint32 format);
int SDL_GetNumVideoDisplays(void);
int SDL_ShowCursor(int toggle);
int SDL_SetRelativeMouseMode(SDL_bool enabled);

/* riscv-stack extension: push an indexed 320x200 frame + palette to the
 * hardware (implemented in compat/lite_bridge.c over SDL_Flip/SDL_SetColors).
 * colors256 points at 256 SDL_Color entries. */
void RVSDL_InitVideo(void);
void RVSDL_PresentIndexed(const void *pixels, int pitch, int w, int h,
                          const void *colors256);

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
	SDL_SCANCODE_SCROLLLOCK = 71,
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
	KMOD_LGUI   = 0x0400, KMOD_RGUI   = 0x0800,
	KMOD_CTRL   = KMOD_LCTRL | KMOD_RCTRL,
	KMOD_SHIFT  = KMOD_LSHIFT | KMOD_RSHIFT,
	KMOD_ALT    = KMOD_LALT | KMOD_RALT,
	KMOD_GUI    = KMOD_LGUI | KMOD_RGUI
} SDL_Keymod;

typedef struct SDL_Keysym {
	SDL_Scancode scancode;
	Sint32       sym;
	Uint16       mod;
	Uint32       unused;
} SDL_Keysym;

SDL_Keymod  SDL_GetModState(void);
const char *SDL_GetScancodeName(SDL_Scancode sc);
SDL_Scancode SDL_GetScancodeFromName(const char *name);

/* -------------------------------------------------------------- events */

#define SDL_QUIT            0x100
#define SDL_WINDOWEVENT     0x200
#define SDL_KEYDOWN         0x300
#define SDL_KEYUP           0x301
#define SDL_TEXTEDITING     0x302
#define SDL_TEXTINPUT       0x303
#define SDL_MOUSEMOTION     0x400
#define SDL_MOUSEBUTTONDOWN 0x401
#define SDL_MOUSEBUTTONUP   0x402

#define SDL_WINDOWEVENT_RESIZED      5
#define SDL_WINDOWEVENT_FOCUS_GAINED 12
#define SDL_WINDOWEVENT_FOCUS_LOST   13

#define SDL_RELEASED 0
#define SDL_PRESSED  1
#define SDL_IGNORE   0

#define SDL_TEXTINPUTEVENT_TEXT_SIZE 32

#define SDL_BUTTON_LEFT   1
#define SDL_BUTTON_MIDDLE 2
#define SDL_BUTTON_RIGHT  3

typedef struct { Uint32 type; Uint8 state; SDL_Keysym keysym; } SDL_KeyboardEvent;
typedef struct { Uint32 type; Uint8 event; } SDL_WindowEvent;
typedef struct { Uint32 type; Sint32 x, y, xrel, yrel; } SDL_MouseMotionEvent;
typedef struct { Uint32 type; Uint8 button; Sint32 x, y; } SDL_MouseButtonEvent;
typedef struct { Uint32 type; char text[SDL_TEXTINPUTEVENT_TEXT_SIZE]; } SDL_TextInputEvent;

typedef union SDL_Event {
	Uint32               type;
	SDL_KeyboardEvent    key;
	SDL_WindowEvent      window;
	SDL_MouseMotionEvent motion;
	SDL_MouseButtonEvent button;
	SDL_TextInputEvent   text;
} SDL_Event;

/* sdl_lite exports symbols named SDL_PollEvent/... with 1.2-shaped events;
 * rename ours at compile time so both can coexist in the link. */
#define SDL_PollEvent SDL2C_PollEvent
#define SDL_PushEvent SDL2C_PushEvent
int SDL2C_PollEvent(SDL_Event *ev);
int SDL2C_PushEvent(const SDL_Event *ev);

/* -------------------------------------------------------------- time */

Uint32 SDL_GetTicks(void);              /* -> sdl_lite */
void   SDL_Delay(Uint32 ms);            /* -> sdl_lite (pumps audio) */

/* -------------------------------------------------------------- audio */

typedef Uint16 SDL_AudioFormat;
#define AUDIO_S8     0x8008
#define AUDIO_S16LSB 0x8010
#define AUDIO_S16SYS AUDIO_S16LSB

#define SDL_AUDIO_ALLOW_FREQUENCY_CHANGE 0x01
#define SDL_AUDIO_ALLOW_FORMAT_CHANGE    0x02
#define SDL_AUDIO_ALLOW_CHANNELS_CHANGE  0x04
#define SDL_AUDIO_ALLOW_SAMPLES_CHANGE   0x08

typedef void (*SDL_AudioCallback)(void *userdata, Uint8 *stream, int len);

typedef struct SDL_AudioSpec {
	int               freq;
	SDL_AudioFormat   format;
	Uint8             channels;
	Uint8             silence;
	Uint16            samples;
	Uint16            padding;
	Uint32            size;
	SDL_AudioCallback callback;
	void             *userdata;
} SDL_AudioSpec;

typedef Uint32 SDL_AudioDeviceID;

SDL_AudioDeviceID SDL_OpenAudioDevice(const char *device, int iscapture,
                                      const SDL_AudioSpec *desired,
                                      SDL_AudioSpec *obtained,
                                      int allowed_changes);
void SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause_on);
void SDL_CloseAudioDevice(SDL_AudioDeviceID dev);
void SDL_LockAudioDevice(SDL_AudioDeviceID dev);
void SDL_UnlockAudioDevice(SDL_AudioDeviceID dev);

typedef struct SDL_AudioCVT {
	int             needed;
	SDL_AudioFormat src_format, dst_format;
	int             src_rate, dst_rate;
	Uint8          *buf;
	int             len;                /* input bytes (caller sets) */
	int             len_cvt;            /* output bytes (after convert) */
	int             len_mult;           /* buf must hold len*len_mult */
	double          len_ratio;
} SDL_AudioCVT;

int SDL_BuildAudioCVT(SDL_AudioCVT *cvt,
                      SDL_AudioFormat src_format, Uint8 src_channels, int src_rate,
                      SDL_AudioFormat dst_format, Uint8 dst_channels, int dst_rate);
int SDL_ConvertAudio(SDL_AudioCVT *cvt);

/* -------------------------------------------------------------- joystick */

typedef struct SDL_Joystick SDL_Joystick;

#define SDL_HAT_CENTERED 0x00
#define SDL_HAT_UP       0x01
#define SDL_HAT_RIGHT    0x02
#define SDL_HAT_DOWN     0x04
#define SDL_HAT_LEFT     0x08

static inline int SDL_NumJoysticks(void) { return 0; }
static inline SDL_Joystick *SDL_JoystickOpen(int i) { (void)i; return (SDL_Joystick *)0; }
static inline void SDL_JoystickClose(SDL_Joystick *j) { (void)j; }
static inline const char *SDL_JoystickName(SDL_Joystick *j) { (void)j; return "none"; }
static inline int SDL_JoystickNumAxes(SDL_Joystick *j) { (void)j; return 0; }
static inline int SDL_JoystickNumButtons(SDL_Joystick *j) { (void)j; return 0; }
static inline int SDL_JoystickNumHats(SDL_Joystick *j) { (void)j; return 0; }
static inline Sint16 SDL_JoystickGetAxis(SDL_Joystick *j, int a) { (void)j; (void)a; return 0; }
static inline Uint8 SDL_JoystickGetButton(SDL_Joystick *j, int b) { (void)j; (void)b; return 0; }
static inline Uint8 SDL_JoystickGetHat(SDL_Joystick *j, int h) { (void)j; (void)h; return SDL_HAT_CENTERED; }
static inline void SDL_JoystickUpdate(void) {}
static inline int SDL_JoystickEventState(int state) { (void)state; return 0; }

#endif /* RVSTACK_SDL2_COMPAT_H */
