/*
 * compat/SDL.h — SDL2 API surface re-created for the RISC-V Stack port of
 * SDLPoP. The game (src/) includes <SDL.h> and gets THIS, never real SDL2.
 *
 * ARCHITECTURE (the link-namespace trap — cost Tyrian/Wolf a segfault):
 * On the PC twin, hal_pc.c + sdl_lite.c are compiled WITHOUT -Icompat and use
 * the GENUINE SDL2. To keep our shim from hijacking that link namespace, every
 * FUNCTION we implement is #define-renamed to a `psdl_` symbol on BOTH targets.
 * TYPES/enums are defined here directly (per-translation-unit; real SDL2's
 * header is never included in a compat TU, so there is no clash).
 *
 * We keep PoP's working surfaces TRUECOLOR (the game composites in 24/32bpp);
 * the single present choke point update_screen() (reimplemented in seg009.c,
 * RVSTACK:) quantizes the final surface to 8bpp indices for the hardware
 * palette + SDL_lite_present_indexed. See PORTING.md.
 *
 * Only the SDL2 surface it uses is provided. Window/renderer/texture/haptic/
 * hint/RWops/messagebox/textinput/timer-callback calls live only inside the
 * seg009.c functions we replace, and are stubbed to no-ops below.
 *
 * SPDX-License-Identifier: BSD-2-Clause (shim); game is GPL-3.0-or-later.
 */
#ifndef POP_COMPAT_SDL_H
#define POP_COMPAT_SDL_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* ---- basic types ---- */
typedef uint8_t  Uint8;
typedef uint16_t Uint16;
typedef uint32_t Uint32;
typedef uint64_t Uint64;
typedef int8_t   Sint8;
typedef int16_t  Sint16;
typedef int32_t  Sint32;
typedef int64_t  Sint64;
typedef enum { SDL_FALSE = 0, SDL_TRUE = 1 } SDL_bool;

/* compile-time assert (SDLPoP checks struct sizes with it) */
#define SDL_COMPILE_TIME_ASSERT(name, x) \
	typedef int SDL_dummy_ ## name[(x) * 2 - 1]

/* ---- color / palette / pixel format ---- */
typedef struct SDL_Color { Uint8 r, g, b, a; } SDL_Color;
typedef struct SDL_Palette { int ncolors; SDL_Color *colors; int refcount; } SDL_Palette;

/* SDL2 pixel-format enum values we actually test (SDL_ISPIXELFORMAT_INDEXED). */
#define SDL_PIXELFORMAT_INDEX8   0x13051001u
#define SDL_PIXELFORMAT_RGB24    0x17101803u
#define SDL_PIXELFORMAT_RGB888   0x16161804u
#define SDL_PIXELFORMAT_ARGB8888 0x16362004u
#define SDL_ISPIXELFORMAT_INDEXED(fmt) \
	((fmt) == SDL_PIXELFORMAT_INDEX8)

typedef struct SDL_PixelFormat {
	Uint32       format;         /* one of the SDL_PIXELFORMAT_* above */
	SDL_Palette *palette;        /* non-NULL only for INDEX8 */
	Uint8        BitsPerPixel;
	Uint8        BytesPerPixel;
	Uint32       Rmask, Gmask, Bmask, Amask;
} SDL_PixelFormat;

typedef struct SDL_Rect { int x, y, w, h; } SDL_Rect;

typedef struct SDL_Surface {
	Uint32           flags;
	SDL_PixelFormat *format;
	int              w, h;
	int              pitch;
	void            *pixels;
	SDL_Rect         clip_rect;
	Uint32           colorkey;   /* index/packed value; valid if SDL_SRCCOLORKEY */
	Uint8            alphamod;    /* SDL_SetSurfaceAlphaMod */
	int              blendmode;
	int              refcount;
} SDL_Surface;
typedef SDL_Surface surface_type_sdl; /* not used; PoP typedefs surface_type itself */

/* surface flags / blend / colorkey */
#define SDL_SWSURFACE     0
#define SDL_SRCCOLORKEY   0x00001000u
#define SDL_SRCALPHA      0x00010000u
#define SDL_BLENDMODE_NONE  0
#define SDL_BLENDMODE_BLEND 1
#define SDL_ALPHA_OPAQUE      255
#define SDL_ALPHA_TRANSPARENT 0

/* ---- scancodes (numeric values MATCH real SDL2 — the game's key tables
 * are indexed by these) ---- */
typedef enum {
	SDL_SCANCODE_UNKNOWN = 0,
	SDL_SCANCODE_A = 4, SDL_SCANCODE_B, SDL_SCANCODE_C, SDL_SCANCODE_D,
	SDL_SCANCODE_E, SDL_SCANCODE_F, SDL_SCANCODE_G, SDL_SCANCODE_H,
	SDL_SCANCODE_I, SDL_SCANCODE_J, SDL_SCANCODE_K, SDL_SCANCODE_L,
	SDL_SCANCODE_M, SDL_SCANCODE_N, SDL_SCANCODE_O, SDL_SCANCODE_P,
	SDL_SCANCODE_Q, SDL_SCANCODE_R, SDL_SCANCODE_S, SDL_SCANCODE_T,
	SDL_SCANCODE_U, SDL_SCANCODE_V, SDL_SCANCODE_W, SDL_SCANCODE_X,
	SDL_SCANCODE_Y, SDL_SCANCODE_Z,
	SDL_SCANCODE_1 = 30, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4,
	SDL_SCANCODE_5, SDL_SCANCODE_6, SDL_SCANCODE_7, SDL_SCANCODE_8,
	SDL_SCANCODE_9, SDL_SCANCODE_0,
	SDL_SCANCODE_RETURN = 40, SDL_SCANCODE_ESCAPE, SDL_SCANCODE_BACKSPACE,
	SDL_SCANCODE_TAB, SDL_SCANCODE_SPACE, SDL_SCANCODE_MINUS,
	SDL_SCANCODE_EQUALS, SDL_SCANCODE_LEFTBRACKET, SDL_SCANCODE_RIGHTBRACKET,
	SDL_SCANCODE_BACKSLASH, SDL_SCANCODE_NONUSHASH, SDL_SCANCODE_SEMICOLON,
	SDL_SCANCODE_APOSTROPHE, SDL_SCANCODE_GRAVE = 53, SDL_SCANCODE_COMMA,
	SDL_SCANCODE_PERIOD, SDL_SCANCODE_SLASH, SDL_SCANCODE_CAPSLOCK = 57,
	SDL_SCANCODE_F1, SDL_SCANCODE_F2, SDL_SCANCODE_F3, SDL_SCANCODE_F4,
	SDL_SCANCODE_F5, SDL_SCANCODE_F6, SDL_SCANCODE_F7, SDL_SCANCODE_F8,
	SDL_SCANCODE_F9, SDL_SCANCODE_F10, SDL_SCANCODE_F11, SDL_SCANCODE_F12,
	SDL_SCANCODE_PRINTSCREEN = 70, SDL_SCANCODE_SCROLLLOCK, SDL_SCANCODE_PAUSE,
	SDL_SCANCODE_INSERT, SDL_SCANCODE_HOME, SDL_SCANCODE_PAGEUP,
	SDL_SCANCODE_DELETE, SDL_SCANCODE_END, SDL_SCANCODE_PAGEDOWN,
	SDL_SCANCODE_RIGHT, SDL_SCANCODE_LEFT, SDL_SCANCODE_DOWN, SDL_SCANCODE_UP,
	SDL_SCANCODE_NUMLOCKCLEAR = 83, SDL_SCANCODE_KP_DIVIDE,
	SDL_SCANCODE_KP_MULTIPLY, SDL_SCANCODE_KP_MINUS, SDL_SCANCODE_KP_PLUS,
	SDL_SCANCODE_KP_ENTER, SDL_SCANCODE_KP_1, SDL_SCANCODE_KP_2,
	SDL_SCANCODE_KP_3, SDL_SCANCODE_KP_4, SDL_SCANCODE_KP_5, SDL_SCANCODE_KP_6,
	SDL_SCANCODE_KP_7, SDL_SCANCODE_KP_8, SDL_SCANCODE_KP_9, SDL_SCANCODE_KP_0,
	SDL_SCANCODE_LCTRL = 224, SDL_SCANCODE_LSHIFT, SDL_SCANCODE_LALT,
	SDL_SCANCODE_LGUI, SDL_SCANCODE_RCTRL, SDL_SCANCODE_RSHIFT,
	SDL_SCANCODE_RALT, SDL_SCANCODE_RGUI,
	SDL_SCANCODE_APPLICATION = 101, SDL_SCANCODE_CLEAR = 156,
	SDL_SCANCODE_MUTE = 127, SDL_SCANCODE_VOLUMEUP = 128,
	SDL_SCANCODE_VOLUMEDOWN = 129, SDL_SCANCODE_AUDIOMUTE = 262,
	SDL_NUM_SCANCODES = 512
} SDL_Scancode;

typedef Sint32 SDL_Keycode;
typedef struct SDL_Keysym {
	SDL_Scancode scancode;
	SDL_Keycode  sym;
	Uint16       mod;
	Uint32       unused;
} SDL_Keysym;

/* key modifiers */
#define KMOD_NONE   0x0000
#define KMOD_LSHIFT 0x0001
#define KMOD_RSHIFT 0x0002
#define KMOD_LCTRL  0x0040
#define KMOD_RCTRL  0x0080
#define KMOD_LALT   0x0100
#define KMOD_RALT   0x0200
#define KMOD_SHIFT  (KMOD_LSHIFT|KMOD_RSHIFT)
#define KMOD_CTRL   (KMOD_LCTRL|KMOD_RCTRL)
#define KMOD_ALT    (KMOD_LALT|KMOD_RALT)
#define KMOD_NUM    0x1000

/* ---- events ---- */
enum {
	SDL_FIRSTEVENT = 0,
	SDL_QUIT = 0x100,
	SDL_WINDOWEVENT = 0x200, SDL_KEYDOWN = 0x300, SDL_KEYUP, SDL_TEXTINPUT = 0x303,
	SDL_MOUSEBUTTONDOWN = 0x401, SDL_MOUSEWHEEL = 0x403,
	SDL_JOYAXISMOTION = 0x600, SDL_JOYBUTTONDOWN = 0x603, SDL_JOYBUTTONUP,
	SDL_CONTROLLERAXISMOTION = 0x650, SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLERBUTTONUP,
	SDL_CONTROLLERDEVICEADDED, SDL_CONTROLLERDEVICEREMOVED,
	SDL_USEREVENT = 0x8000
};
/* window sub-events */
enum { SDL_WINDOWEVENT_EXPOSED = 3, SDL_WINDOWEVENT_MOVED = 4,
	SDL_WINDOWEVENT_SIZE_CHANGED = 6, SDL_WINDOWEVENT_MINIMIZED = 7,
	SDL_WINDOWEVENT_RESTORED = 9, SDL_WINDOWEVENT_FOCUS_GAINED = 12 };

typedef struct { Uint32 type; Uint32 timestamp; Uint32 windowID;
	Uint8 state, repeat, padding2, padding3; SDL_Keysym keysym; } SDL_KeyboardEvent;
typedef struct { Uint32 type; Uint32 timestamp; Sint32 which;
	Uint8 button, state, padding1, padding2; } SDL_ControllerButtonEvent;
typedef struct { Uint32 type; Uint32 timestamp; Sint32 which;
	Uint8 axis, p1, p2, p3; Sint16 value, p4; } SDL_ControllerAxisEvent;
typedef struct { Uint32 type; Uint32 timestamp; Sint32 which;
	Uint8 axis, p1, p2, p3; Sint16 value, p4; } SDL_JoyAxisEvent;
typedef struct { Uint32 type; Uint32 timestamp; Sint32 which;
	Uint8 button, state, p1, p2; } SDL_JoyButtonEvent;
typedef struct { Uint32 type; Uint32 timestamp; Sint32 which; } SDL_ControllerDeviceEvent;
typedef struct { Uint32 type; Uint32 timestamp; Uint32 windowID;
	Uint8 event, p1, p2, p3; Sint32 data1, data2; } SDL_WindowEvent;
typedef struct { Uint32 type; Uint32 timestamp; Uint32 windowID;
	Sint32 code; void *data1, *data2; } SDL_UserEvent;
typedef struct { Uint32 type; Uint32 timestamp; Uint32 windowID;
	char text[32]; } SDL_TextInputEvent;
typedef struct { Uint32 type; Uint32 timestamp; Uint32 windowID; Uint32 which;
	Uint8 button, state, clicks, padding1; Sint32 x, y; } SDL_MouseButtonEvent;
typedef struct { Uint32 type; Uint32 timestamp; Uint32 windowID; Uint32 which;
	Sint32 x, y; Uint32 direction; } SDL_MouseWheelEvent;

typedef union SDL_Event {
	Uint32 type;
	SDL_KeyboardEvent key;
	SDL_TextInputEvent text;
	SDL_MouseButtonEvent button;
	SDL_MouseWheelEvent wheel;
	SDL_ControllerButtonEvent cbutton;
	SDL_ControllerAxisEvent caxis;
	SDL_JoyAxisEvent jaxis;
	SDL_JoyButtonEvent jbutton;
	SDL_ControllerDeviceEvent cdevice;
	SDL_WindowEvent window;
	SDL_UserEvent user;
	Uint8 padding[56];
} SDL_Event;

/* mouse buttons + string hints (inert on this platform) */
#define SDL_BUTTON_MIDDLE 2
#define SDL_BUTTON_X1 4
#define SDL_BUTTON_X2 5
#define SDL_HINT_RENDER_SCALE_QUALITY "SDL_RENDER_SCALE_QUALITY"
#define SDL_HINT_RENDER_VSYNC         "SDL_RENDER_VSYNC"
#define SDL_HINT_IME_SHOW_UI          "SDL_IME_SHOW_UI"
#define SDL_HINT_WINDOWS_DISABLE_THREAD_NAMING "SDL_WDTN"
typedef struct SDL_RendererInfo {
	const char *name; Uint32 flags; Uint32 num_texture_formats;
	Uint32 texture_formats[16]; int max_texture_width, max_texture_height;
} SDL_RendererInfo;

#ifndef alloca
#define alloca __builtin_alloca
#endif

/* ---- audio ---- */
typedef Uint16 SDL_AudioFormat;
#define AUDIO_U8     0x0008
#define AUDIO_S16    0x8010
#define AUDIO_S16SYS 0x8010
#define AUDIO_S16LSB 0x8010
typedef void (*SDL_AudioCallback)(void *userdata, Uint8 *stream, int len);
typedef struct SDL_AudioSpec {
	int freq; SDL_AudioFormat format; Uint8 channels, silence;
	Uint16 samples; Uint16 padding; Uint32 size;
	SDL_AudioCallback callback; void *userdata;
} SDL_AudioSpec;
typedef struct SDL_AudioCVT { int needed; SDL_AudioFormat src_format, dst_format;
	double rate_incr; Uint8 *buf; int len, len_cvt, len_mult; double len_ratio;
	void (*filters[10])(void); int filter_index; } SDL_AudioCVT;
enum { SDL_AUDIO_STOPPED = 0, SDL_AUDIO_PLAYING, SDL_AUDIO_PAUSED };

/* ---- opaque platform handles (all our impls are no-ops) ---- */
typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;
typedef struct SDL_GameController SDL_GameController;
typedef struct SDL_Joystick SDL_Joystick;
typedef struct SDL_Haptic SDL_Haptic;
typedef struct SDL_RWops SDL_RWops;
typedef int SDL_TimerID;
typedef Uint32 (*SDL_TimerCallback)(Uint32 interval, void *param);

/* GameController/joystick enums (values match SDL2) */
enum { SDL_CONTROLLER_BUTTON_A = 0, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X,
	SDL_CONTROLLER_BUTTON_Y, SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_GUIDE,
	SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_LEFTSTICK,
	SDL_CONTROLLER_BUTTON_RIGHTSTICK, SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
	SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, SDL_CONTROLLER_BUTTON_DPAD_UP,
	SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_LEFT,
	SDL_CONTROLLER_BUTTON_DPAD_RIGHT };
enum { SDL_CONTROLLER_AXIS_LEFTX = 0, SDL_CONTROLLER_AXIS_LEFTY,
	SDL_CONTROLLER_AXIS_RIGHTX, SDL_CONTROLLER_AXIS_RIGHTY,
	SDL_CONTROLLER_AXIS_TRIGGERLEFT, SDL_CONTROLLER_AXIS_TRIGGERRIGHT };

/* Init flags / misc constants referenced but inert */
#define SDL_INIT_VIDEO 0x20u
#define SDL_INIT_TIMER 0x1u
#define SDL_INIT_AUDIO 0x10u
#define SDL_INIT_GAMECONTROLLER 0x2000u
#define SDL_INIT_HAPTIC 0x1000u
#define SDL_INIT_NOPARACHUTE 0x100000u
#define SDL_WINDOWPOS_UNDEFINED 0x1FFF0000u
#define SDL_WINDOW_FULLSCREEN_DESKTOP 0x1001u
#define SDL_WINDOW_RESIZABLE 0x20u
#define SDL_WINDOW_ALLOW_HIGHDPI 0x2000u
#define SDL_RENDERER_SOFTWARE 0x1u
#define SDL_RENDERER_ACCELERATED 0x2u
#define SDL_RENDERER_TARGETTEXTURE 0x8u
#define SDL_TEXTUREACCESS_STREAMING 1
#define SDL_TEXTUREACCESS_TARGET 2
#define SDL_DISABLE 0
#define SDL_ENABLE 1
#define SDL_BUTTON_LEFT 1
#define SDL_BUTTON_RIGHT 3
#define SDL_MESSAGEBOX_ERROR       0x10
#define SDL_MESSAGEBOX_WARNING     0x20
#define SDL_MESSAGEBOX_INFORMATION 0x40

/* byte-order helpers (target is little-endian) */
#define SDL_SwapLE16(x) ((Uint16)(x))
#define SDL_SwapLE32(x) ((Uint32)(x))
#define SDL_SwapBE16(x) ((Uint16)(((x)<<8)|((Uint16)(x)>>8)))
#define SDL_SwapBE32(x) __builtin_bswap32((Uint32)(x))
#define SDL_memset memset
#define SDL_free    free
#define SDL_strlen  strlen

/* libc gaps that picolibc-minimal (console) and strict libc headers (twin)
 * don't declare for the SDLPoP sources; implemented by compat/libc_shim.c on
 * the console, present in glibc on the twin. */
int strcasecmp(const char *a, const char *b);
int strncasecmp(const char *a, const char *b, size_t n);

/* SDL version (some code checks SDL_VERSION_ATLEAST) */
typedef struct { Uint8 major, minor, patch; } SDL_version;
#define SDL_MAJOR_VERSION 2
#define SDL_MINOR_VERSION 0
#define SDL_PATCHLEVEL 20
#define SDL_VERSIONNUM(X,Y,Z) ((X)*1000+(Y)*100+(Z))
#define SDL_COMPILEDVERSION SDL_VERSIONNUM(2,0,20)
#define SDL_VERSION_ATLEAST(X,Y,Z) (SDL_COMPILEDVERSION >= SDL_VERSIONNUM(X,Y,Z))
#define SDL_VERSION(v) do { (v)->major=2;(v)->minor=0;(v)->patch=20; } while(0)

/* ============================================================================
 * FUNCTION NAMESPACE: everything we implement is psdl_* on BOTH targets so it
 * never collides with genuine SDL2 (PC twin) or sdl_lite's SDL_* (console).
 * ==========================================================================*/
#define SDL_CreateRGBSurface     psdl_CreateRGBSurface
#define SDL_CreateRGBSurfaceWithFormat psdl_CreateRGBSurfaceWithFormat
#define SDL_ConvertSurface       psdl_ConvertSurface
#define SDL_ConvertSurfaceFormat psdl_ConvertSurfaceFormat
#define SDL_FreeSurface          psdl_FreeSurface
#define SDL_BlitSurface          psdl_BlitSurface
#define SDL_UpperBlit            psdl_BlitSurface
#define SDL_BlitScaled           psdl_BlitScaled
#define SDL_FillRect             psdl_FillRect
#define SDL_SetColorKey          psdl_SetColorKey
#define SDL_SetSurfaceBlendMode  psdl_SetSurfaceBlendMode
#define SDL_SetSurfaceAlphaMod   psdl_SetSurfaceAlphaMod
#define SDL_SetAlpha             psdl_SetAlpha
#define SDL_SetClipRect          psdl_SetClipRect
#define SDL_MapRGB               psdl_MapRGB
#define SDL_MapRGBA              psdl_MapRGBA
#define SDL_SetPaletteColors     psdl_SetPaletteColors
#define SDL_SetSurfacePalette    psdl_SetSurfacePalette
#define SDL_SetColors            psdl_SetColors
#define SDL_LockSurface          psdl_LockSurface
#define SDL_UnlockSurface        psdl_UnlockSurface
#define SDL_GetError             psdl_GetError
#define SDL_Init                 psdl_Init
#define SDL_InitSubSystem        psdl_InitSubSystem
#define SDL_Quit                 psdl_Quit
#define SDL_PollEvent            psdl_PollEvent
#define SDL_PushEvent            psdl_PushEvent
#define SDL_GetKeyboardState     psdl_GetKeyboardState
#define SDL_GetMouseState        psdl_GetMouseState
#define SDL_GetTicks             psdl_GetTicks
#define SDL_Delay                psdl_Delay
#define SDL_GetPerformanceCounter   psdl_GetPerformanceCounter
#define SDL_GetPerformanceFrequency psdl_GetPerformanceFrequency
#define SDL_OpenAudio            psdl_OpenAudio
#define SDL_CloseAudio           psdl_CloseAudio
#define SDL_PauseAudio           psdl_PauseAudio
#define SDL_LockAudio            psdl_LockAudio
#define SDL_UnlockAudio          psdl_UnlockAudio
#define SDL_GetAudioStatus       psdl_GetAudioStatus
#define SDL_ShowCursor           psdl_ShowCursor
#define SDL_ShowSimpleMessageBox psdl_ShowSimpleMessageBox
#define SDL_SetHint              psdl_SetHint
#define SDL_GetScancodeName      psdl_GetScancodeName
#define SDL_GetPixelFormatName   psdl_GetPixelFormatName

/* Declarations for the functions above. */
SDL_Surface *psdl_CreateRGBSurface(Uint32 flags, int w, int h, int depth,
	Uint32 rm, Uint32 gm, Uint32 bm, Uint32 am);
SDL_Surface *psdl_CreateRGBSurfaceWithFormat(Uint32 flags, int w, int h,
	int depth, Uint32 format);
SDL_Surface *psdl_ConvertSurface(SDL_Surface *s, const SDL_PixelFormat *fmt, Uint32 flags);
SDL_Surface *psdl_ConvertSurfaceFormat(SDL_Surface *s, Uint32 fmt, Uint32 flags);
void psdl_FreeSurface(SDL_Surface *s);
int  psdl_BlitSurface(SDL_Surface *src, const SDL_Rect *srect, SDL_Surface *dst, SDL_Rect *drect);
int  psdl_BlitScaled(SDL_Surface *src, const SDL_Rect *srect, SDL_Surface *dst, SDL_Rect *drect);
int  psdl_FillRect(SDL_Surface *dst, const SDL_Rect *rect, Uint32 color);
int  psdl_SetColorKey(SDL_Surface *s, int flag, Uint32 key);
int  psdl_SetSurfaceBlendMode(SDL_Surface *s, int mode);
int  psdl_SetSurfaceAlphaMod(SDL_Surface *s, Uint8 alpha);
int  psdl_SetAlpha(SDL_Surface *s, Uint32 flag, Uint8 alpha);
int  psdl_SetClipRect(SDL_Surface *s, const SDL_Rect *rect);
Uint32 psdl_MapRGB(const SDL_PixelFormat *fmt, Uint8 r, Uint8 g, Uint8 b);
Uint32 psdl_MapRGBA(const SDL_PixelFormat *fmt, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
int  psdl_SetPaletteColors(SDL_Palette *pal, const SDL_Color *colors, int first, int n);
int  psdl_SetSurfacePalette(SDL_Surface *s, SDL_Palette *pal);
int  psdl_SetColors(SDL_Surface *s, SDL_Color *colors, int first, int n);
int  psdl_LockSurface(SDL_Surface *s);
void psdl_UnlockSurface(SDL_Surface *s);
const char *psdl_GetError(void);
int  psdl_Init(Uint32 flags);
int  psdl_InitSubSystem(Uint32 flags);
void psdl_Quit(void);
int  psdl_PollEvent(SDL_Event *ev);
int  psdl_PushEvent(SDL_Event *ev);
const Uint8 *psdl_GetKeyboardState(int *numkeys);
Uint32 psdl_GetMouseState(int *x, int *y);
Uint32 psdl_GetTicks(void);
void psdl_Delay(Uint32 ms);
Uint64 psdl_GetPerformanceCounter(void);
Uint64 psdl_GetPerformanceFrequency(void);
int  psdl_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained);
void psdl_CloseAudio(void);
void psdl_PauseAudio(int pause_on);
void psdl_LockAudio(void);
void psdl_UnlockAudio(void);
int  psdl_GetAudioStatus(void);
int  psdl_ShowCursor(int toggle);
int  psdl_ShowSimpleMessageBox(Uint32 flags, const char *title, const char *msg, SDL_Window *w);
SDL_bool psdl_SetHint(const char *name, const char *value);
const char *psdl_GetScancodeName(SDL_Scancode sc);
const char *psdl_GetPixelFormatName(Uint32 fmt);

/* ============================================================================
 * PLATFORM NO-OPS: window/renderer/texture/controller/haptic/timer/RWops calls
 * live only inside the seg009.c functions we neutralize (set_gr_mode,
 * init_scaling, update_screen present path). They must COMPILE but do nothing.
 * Create/Open return truthy dummy handles; the few output-writers are inline
 * stubs that fill sane 320x200 defaults. Renamed like the rest to avoid the
 * real-SDL2 link namespace on the PC twin.
 * ==========================================================================*/
#define SDL_CreateWindow(...)              ((SDL_Window*)1)
#define SDL_CreateRenderer(...)            ((SDL_Renderer*)1)
#define SDL_CreateTexture(...)             ((SDL_Texture*)1)
#define SDL_DestroyTexture(...)            ((void)0)
#define SDL_DestroyRenderer(...)           ((void)0)
#define SDL_DestroyWindow(...)             ((void)0)
#define SDL_SetRenderTarget(...)           (0)
#define SDL_RenderClear(...)               (0)
#define SDL_RenderCopy(...)                (0)
#define SDL_RenderCopyEx(...)              (0)
#define SDL_RenderPresent(...)             ((void)0)
#define SDL_UpdateTexture(...)             (0)
#define SDL_RenderSetLogicalSize(...)      (0)
#define SDL_RenderSetIntegerScale(...)     (0)
#define SDL_RenderGetViewport(...)         ((void)0)
#define SDL_RenderGetLogicalSize(...)      ((void)0)
#define SDL_UpdateRect(...)                ((void)0)
#define SDL_SetWindowTitle(...)            ((void)0)
#define SDL_SetWindowIcon(...)             ((void)0)
#define SDL_SetWindowFullscreen(...)       (0)
#define SDL_GetWindowFlags(...)            (0u)
#define SDL_WM_SetCaption(...)             ((void)0)
#define SDL_GameControllerOpen(...)        ((SDL_GameController*)1)
#define SDL_GameControllerClose(...)       ((void)0)
#define SDL_GameControllerFromInstanceID(...) ((SDL_GameController*)1)
#define SDL_GameControllerAddMappingsFromFile(...) (0)
#define SDL_GameControllerAddMapping(...)  (0)
#define SDL_GameControllerName(...)        ("pad")
#define SDL_GameControllerRumble(...)      (-1)
#define SDL_IsGameController(...)          (SDL_TRUE)
#define SDL_NumJoysticks(...)              (1)
#define SDL_JoystickOpen(...)              ((SDL_Joystick*)1)
#define SDL_JoystickRumble(...)            (-1)
#define SDL_JoystickInstanceID(...)        (0)
#define SDL_JoystickNumButtons(...)        (0)
#define SDL_JoystickNumAxes(...)           (0)
#define SDL_HapticOpen(...)                ((SDL_Haptic*)0)
#define SDL_HapticRumbleInit(...)          (-1)
#define SDL_HapticRumblePlay(...)          (-1)
#define SDL_HapticClose(...)               ((void)0)
#define SDL_AddTimer(...)                  (1)
#define SDL_RemoveTimer(...)               (SDL_TRUE)
#define SDL_RWFromFile(...)                ((SDL_RWops*)0)
#define SDL_RWwrite(...)                   (0)
#define SDL_iconv_string(...)              ((char*)0)
#define SDL_StartTextInput(...)            ((void)0)
#define SDL_StopTextInput(...)             ((void)0)
#define SDL_SetTextInputRect(...)          ((void)0)
#define SDL_EnableKeyRepeat(...)           (0)
#define SDL_EnableUNICODE(...)             (0)
#define SDL_BuildAudioCVT(...)             (0)
#define SDL_ConvertAudio(...)              (0)
#define SDL_wcslen(s)                      (0u)
#define SDL_GetVersion(v)                  SDL_VERSION(v)
#define IMG_Load(...)                      ((SDL_Surface*)0)   /* icon: dropped */
#define IMG_SavePNG(...)                   (-1)
#define IMG_GetError(...)                  ("")

/* Real PNG path (loose data/<SET>/resNNN.png sprites, indexed): a memory RWops
 * + lodepng-backed IMG_Load_RW that preserves the palette (compat/pop_png.c).
 * Renamed like everything else to dodge real SDL2/SDL_image on the PC twin. */
#define SDL_RWFromConstMem psdl_RWFromConstMem
#define SDL_RWFromMem      psdl_RWFromConstMem
#define SDL_RWread         psdl_RWread
#define SDL_RWtell         psdl_RWtell
#define SDL_RWclose        psdl_RWclose
#define IMG_Load_RW        psdl_IMG_Load_RW
SDL_RWops *psdl_RWFromConstMem(const void *mem, int size);
size_t psdl_RWread(SDL_RWops *rw, void *ptr, size_t size, size_t maxnum);
long   psdl_RWtell(SDL_RWops *rw);
int    psdl_RWclose(SDL_RWops *rw);
SDL_Surface *psdl_IMG_Load_RW(SDL_RWops *rw, int freesrc);

/* output-writing stubs (fill 320x200 defaults so scaling math stays sane) */
static inline void SDL_GetWindowSize(SDL_Window *w, int *ww, int *hh)
{ (void)w; if (ww) *ww = 320; if (hh) *hh = 200; }
static inline int SDL_GetRendererOutputSize(SDL_Renderer *r, int *ww, int *hh)
{ (void)r; if (ww) *ww = 320; if (hh) *hh = 200; return 0; }
static inline void SDL_GL_GetDrawableSize(SDL_Window *w, int *ww, int *hh)
{ (void)w; if (ww) *ww = 320; if (hh) *hh = 200; }
static inline void SDL_RenderGetScale(SDL_Renderer *r, float *sx, float *sy)
{ (void)r; if (sx) *sx = 1.0f; if (sy) *sy = 1.0f; }
static inline int SDL_GetRendererInfo(SDL_Renderer *r, void *info)
{ (void)r; (void)info; return 0; }

#endif /* POP_COMPAT_SDL_H */
