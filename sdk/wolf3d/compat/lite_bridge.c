/*
 * lite_bridge.c — the ONLY compat file that includes sdk/sdl_lite.h.
 * Translates the plain-typed bridge calls (rv_bridge.h) into sdl_lite
 * calls; compat/wsdl.c sits on the other side with the SDL2-shaped types.
 *
 * Pad -> Wolf3D keys (through sdl_lite's pad-to-keysym map, then the
 * sym->SDL2-scancode table below — Wolf's default bindings are kept):
 *
 *   d-pad  -> arrows        move / menu navigation
 *   A      -> LCtrl         fire (Wolf default bt_attack)
 *   B      -> Space         open door / use
 *   X      -> LAlt          strafe modifier
 *   Y      -> RShift        run (Wolf default bt_run = sc_LShift; RShift
 *                           IN_MapKey-folds to LShift)
 *   R1     -> P             pause
 *   SELECT -> Escape        menu open / back out
 *   START  -> Return        menu confirm
 *   SELECT+START            exit to game picker (sdl_lite convention)
 *
 * Part of the Wolf4SDL riscv-stack port glue (see compat/SDL.h).
 */
#include "hal.h"                 /* sys_diag (load beacons) */
#include "rv_bridge.h"

#include "sdl_lite.h"

#include <string.h>

/* SDL2 scancode values (fixed constants; also in compat/SDL.h) */
#define SC_UP     82
#define SC_DOWN   81
#define SC_LEFT   80
#define SC_RIGHT  79
#define SC_SPACE  44
#define SC_RETURN 40
#define SC_ESCAPE 41
#define SC_LCTRL  224
#define SC_LALT   226
#define SC_RSHIFT 229
#define SC_P      19

static SDL_Surface *lite_screen;

void rvb_video_init(void)
{
	if (lite_screen)
		return;
	lite_screen = SDL_SetVideoMode(320, 200, 8, 0);

	static const SDLKey map[16] = {
		SDLK_UP, SDLK_DOWN, SDLK_LEFT, SDLK_RIGHT,     /* d-pad */
		SDLK_LCTRL, SDLK_SPACE,                         /* A, B */
		SDLK_LALT, SDLK_s,                              /* X, Y */
		SDLK_UNKNOWN, SDLK_p,                           /* L1, R1 */
		SDLK_UNKNOWN, SDLK_UNKNOWN, SDLK_UNKNOWN, SDLK_UNKNOWN,
		SDLK_ESCAPE, SDLK_RETURN,                       /* SELECT, START */
	};
	SDL_lite_set_keymap(map);
#ifndef WOLF_NOHUD
	SDL_lite_stats(1);                  /* dev readout: MS xx.x F yy */
#endif
}

/* Load-progress beacon: same purpose as Tyrian's (a photo of a stuck
 * screen on hardware tells us which load stage died). Palette entries
 * 248..254 are re-asserted bright every call so it survives fades. */
void rvb_progress(int stage)
{
	sys_diag(0xBEAC0000u | (unsigned)stage);
	if (!lite_screen)
		return;
	static const SDL_Color bright[7] = {
		{255,255,255,0},{255,64,64,0},{255,255,0,0},{0,255,0,0},
		{0,255,255,0},{64,128,255,0},{255,0,255,0},
	};
	SDL_SetColors(lite_screen, bright, 248, 7);
	uint8_t *px = lite_screen->pixels;
	for (int y = 0; y < 4; y++)
		for (int x = 0; x < 8 * 8; x++)
			px[y * lite_screen->pitch + x] =
			    (x / 8 <= stage) ? (uint8_t)(248 + (x / 8)) : 0;
	SDL_Flip(lite_screen);
}

void rvb_present_indexed(const void *pixels, int pitch, int w, int h,
                         const void *colors256)
{
	if (!lite_screen)
		return;
	/* zero-copy path: screenBuffer is Wolf's own stable surface (the
	 * shadow-surface copy would be pure overhead — Tyrian lesson) */
	SDL_lite_present_indexed(pixels, pitch, w, h, colors256);
}

int rvb_poll_key(int *scancode)
{
	static const int sym2sc[SDLK_LAST] = {
		[SDLK_UP]     = SC_UP,     [SDLK_DOWN]   = SC_DOWN,
		[SDLK_LEFT]   = SC_LEFT,   [SDLK_RIGHT]  = SC_RIGHT,
		[SDLK_SPACE]  = SC_SPACE,  [SDLK_RETURN] = SC_RETURN,
		[SDLK_ESCAPE] = SC_ESCAPE, [SDLK_LCTRL]  = SC_LCTRL,
		[SDLK_LALT]   = SC_LALT,   [SDLK_p]      = SC_P,
		[SDLK_s]      = SC_RSHIFT,              /* Y pad btn = run */
	};
	SDL_Event ev;
	while (SDL_PollEvent(&ev)) {
		if ((ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) &&
		    ev.key.keysym.sym > 0 && ev.key.keysym.sym < SDLK_LAST) {
			*scancode = sym2sc[ev.key.keysym.sym];
			if (*scancode)
				return ev.type == SDL_KEYDOWN ? 1 : 2;
		}
	}
	return 0;
}

int rvb_audio_open(int channels, int samples,
                   void (*cb)(void *ud, uint8_t *stream, int len), void *ud)
{
	SDL_AudioSpec want;
	memset(&want, 0, sizeof(want));
	want.freq     = 48000;
	want.channels = (Uint8)(channels == 2 ? 2 : 1);
	want.samples  = (Uint16)samples;
	want.callback = cb;
	want.userdata = ud;
	if (SDL_OpenAudio(&want, 0) != 0)
		return -1;
	return 48000;
}

void rvb_audio_pause(int pause_on) { SDL_PauseAudio(pause_on); }
void rvb_audio_close(void)         { SDL_CloseAudio(); }
