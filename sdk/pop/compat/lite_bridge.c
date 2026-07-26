/*
 * lite_bridge.c — the ONLY compat file that includes sdk/sdl_lite.h.
 * Translates the plain-typed bridge calls (rv_bridge.h) into sdl_lite calls;
 * compat/pop_sdl.c sits on the other side with the SDL2-shaped types.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include "hal.h"                 /* sys_diag (load beacons) */
#include "rv_bridge.h"

#include "sdl_lite.h"

#include <string.h>

static SDL_Surface *lite_screen;

void rvb_video_init(void)
{
	if (lite_screen)
		return;
	lite_screen = SDL_SetVideoMode(320, 200, 8, 0);
	/* PoP feeds input as synthesized SDL2 controller events (pop_sdl.c reads
	 * the HAL pad directly), so sdl_lite's own pad->keysym map is unused. */
#ifdef POP_DEBUG_AIDS
	SDL_lite_stats(1);                  /* dev readout: MS xx.x F yy.
	                                     * Also forces palette entry 255 to
	                                     * white (sdl_lite owns it while the
	                                     * HUD is up) — off by default so the
	                                     * game's own palette is untouched. */
#endif
}

/* Load-progress beacon (Tyrian/Wolf convention: a photo of a stuck screen on
 * hardware names the load stage that died). Entries 248..254 re-asserted
 * bright each call so it survives PoP's fades. */
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
		rvb_video_init();
	SDL_lite_present_indexed(pixels, pitch, w, h, colors256);
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
	SDL_PauseAudio(0);
	return 48000;
}

void rvb_audio_pause(int pause_on) { SDL_PauseAudio(pause_on); }
void rvb_audio_close(void)         { SDL_CloseAudio(); }
void rvb_audio_pump(void)          { SDL_lite_audio_pump(); }
