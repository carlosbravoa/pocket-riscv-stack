/*
 * lite_bridge.c — the ONLY compat file that includes sdk/sdl_lite.h.
 * Translates the plain-typed bridge calls (rv_bridge.h) into sdl_lite
 * calls; sdl2_shim.c sits on the other side with the SDL2-shaped types.
 *
 * GPL-2.0-or-later (port glue; see compat/SDL.h).
 */
#include "hal.h"                 /* sys_ticks_us (frame HUD) */
#include "rv_bridge.h"

#include "sdl_lite.h"

#include <string.h>

/* SDL2 scancode values for the keys sdl_lite's pad map can produce
 * (numbers are fixed SDL2 constants; also defined in compat/SDL.h). */
#define SC_UP 82
#define SC_DOWN 81
#define SC_LEFT 80
#define SC_RIGHT 79
#define SC_SPACE 44
#define SC_RETURN 40
#define SC_ESCAPE 41
#define SC_LCTRL 224
#define SC_LALT 226
#define SC_P 19
#define SC_S 22

static SDL_Surface *lite_screen;

void rvb_video_init(void)
{
	if (lite_screen)
		return;
	lite_screen = SDL_SetVideoMode(320, 200, 8, 0);

	/* Pad -> Tyrian keys (defaults: SPACE=fire, RETURN=rear-weapon mode /
	 * confirm, LCTRL/LALT=sidekicks, ESC=menu, p=pause):
	 *   A=fire  B=rear mode  X/Y=sidekicks  R1=pause  SELECT=menu  START=ok */
	static const SDLKey map[16] = {
		SDLK_UP, SDLK_DOWN, SDLK_LEFT, SDLK_RIGHT,     /* d-pad */
		SDLK_SPACE, SDLK_RETURN,                        /* A, B */
		SDLK_LCTRL, SDLK_LALT,                          /* X, Y */
		SDLK_s, SDLK_p,                                 /* L1, R1 */
		SDLK_UNKNOWN, SDLK_UNKNOWN, SDLK_UNKNOWN, SDLK_UNKNOWN,
		SDLK_ESCAPE, SDLK_RETURN,                       /* SELECT, START */
	};
	SDL_lite_set_keymap(map);
#ifndef TYRIAN_NOHUD
	SDL_lite_stats(1);                  /* dev readout: MS xx.x F yy */
#endif
}

/* Load-progress beacon: paint a small colored bar top-left and present NOW.
 * Attract/level loads take real time at 50 MHz; without this a long load is
 * indistinguishable from a hang on hardware (v0.17.9 field report). Colors
 * step 1..N through the load sequence — a photo of the stuck screen tells
 * us exactly which stage died. Also pumps audio (SDL_Flip does). */
void rvb_progress(int stage)
{
	if (!lite_screen)
		return;
	/* the beacon must survive fade_black: it paints with palette indexes
	 * 248..254 and RE-ASSERTS those entries as bright colors every call
	 * (v0.17.10's version used game palette entries — invisible on the
	 * faded-to-black screen it most needed to appear on) */
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
	/* frame-time HUD (speed stage): bottom row bar, 1 px per ms between
	 * presents, tick marks at 16/33 ms. Palette entries 248.. are the
	 * beacon's self-asserted brights. Compile out with TYRIAN_NOHUD. */
/* frame stats: the shim's numeric HUD (SDL_lite_stats in rvb_init) */
	/* zero-copy path: VGAScreen is Tyrian's own stable surface, so the
	 * shadow-buffer copy was pure overhead (one full-screen memcpy/frame,
	 * ~2 ms at 50 MHz — part of the "anything moving is slow" report) */
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
		[SDLK_s]      = SC_S,
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
