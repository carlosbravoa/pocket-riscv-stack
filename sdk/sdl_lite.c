// sdl_lite — see sdl_lite.h. SDL 1.2's working set over the HAL.
// SPDX-License-Identifier: BSD-2-Clause
#include "sdl_lite.h"
#include "hal.h"
#ifndef RVSTACK_PC
#include <system.h>              /* flush_cpu_dcache_range (blit present) */
#else
#define flush_cpu_dcache_range(p, n) ((void)0)   /* PC: coherent */
#endif
#include <string.h>
#include <stdlib.h>

// ---------------------------------------------------------------- video
//
// Ports draw into a stable shadow surface (SDL semantics: pixels survive
// Flip); SDL_Flip copies it into the HAL back buffer, letterboxed when the
// mode is shorter than the panel (320x200 -> rows 20..219), then presents.

static SDL_Surface screen;
static uint8_t    *shadow;
static int         letterbox_y;
static uint32_t    ev_last_poll_us; // input pass gate (events section below)
static void stats_tick(uint8_t *fb, int fw);   // dev HUD (stats section)
static int stats_on;                           // (defined in stats section)

SDL_Surface *SDL_SetVideoMode(int w, int h, int bpp, Uint32 flags)
{
	(void)flags;
	if (bpp != 8 || w > fb_width() || h > fb_height())
		return 0;
	if (!shadow)
		shadow = malloc((size_t)w * h);
	if (!shadow)
		return 0;
	memset(shadow, 0, (size_t)w * h);
	screen.pixels = shadow;
	screen.w      = w;
	screen.h      = h;
	screen.pitch  = w;
	letterbox_y   = (fb_height() - h) / 2;
	return &screen;
}

int SDL_Flip(SDL_Surface *s)
{
	uint8_t *fb = fb_backbuffer();
	int fw = fb_width();
	if (letterbox_y) {
		memset(fb, 0, (size_t)fw * letterbox_y);
		memset(fb + (letterbox_y + s->h) * fw, 0,
		       (size_t)fw * (fb_height() - s->h - letterbox_y));
	}
	const uint8_t *src = s->pixels;
	uint8_t *dst = fb + letterbox_y * fw + (fw - s->w) / 2;
	for (int y = 0; y < s->h; y++, src += s->pitch, dst += fw)
		memcpy(dst, src, s->w);
	stats_tick(fb + letterbox_y * fw, fw);
	SDL_lite_audio_pump();              // no interrupts: piggyback on vsync
	fb_present();
	ev_last_poll_us = 0;                // a flip reopens the input gate
	return 0;
}

void SDL_SetColors(SDL_Surface *s, const SDL_Color *colors, int first, int n)
{
	(void)s;
	static uint8_t pal[256][3];
	for (int i = 0; i < n; i++) {
		pal[first + i][0] = colors[i].r;
		pal[first + i][1] = colors[i].g;
		pal[first + i][2] = colors[i].b;
	}
	if (stats_on) {
		// the HUD owns entry 255 while profiling — games whose palette
		// makes 255 black rendered the readout invisible (jukebox, v0.18.3)
		pal[255][0] = pal[255][1] = pal[255][2] = 255;
	}
	palette_set(pal);
}

SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int w, int h, int bpp,
                                  Uint32 rm, Uint32 gm, Uint32 bm, Uint32 am)
{
	(void)flags; (void)rm; (void)gm; (void)bm; (void)am;
	if (bpp != 8)
		return 0;
	SDL_Surface *s = malloc(sizeof(*s));
	if (!s)
		return 0;
	s->pixels = malloc((size_t)w * h);
	if (!s->pixels) {
		free(s);
		return 0;
	}
	s->w = w; s->h = h; s->pitch = w;
	return s;
}

void SDL_FreeSurface(SDL_Surface *s)
{
	if (!s || s == &screen)
		return;
	free(s->pixels);
	free(s);
}

int SDL_BlitSurface(SDL_Surface *src, const SDL_Rect *sr,
                    SDL_Surface *dst, SDL_Rect *dr)
{
	SDL_Rect s = sr ? *sr : (SDL_Rect){0, 0, (Uint16)src->w, (Uint16)src->h};
	int dx = dr ? dr->x : 0, dy = dr ? dr->y : 0;
	for (int y = 0; y < s.h; y++) {
		int sy = s.y + y, ty = dy + y;
		if (sy < 0 || sy >= src->h || ty < 0 || ty >= dst->h)
			continue;
		int   w  = s.w;
		int   sx = s.x, tx = dx;
		if (tx < 0) { sx -= tx; w += tx; tx = 0; }
		if (tx + w > dst->w) w = dst->w - tx;
		if (sx + w > src->w) w = src->w - sx;
		if (w > 0)
			memcpy((uint8_t *)dst->pixels + ty * dst->pitch + tx,
			       (const uint8_t *)src->pixels + sy * src->pitch + sx, w);
	}
	return 0;
}

int SDL_FillRect(SDL_Surface *dst, const SDL_Rect *r, Uint32 color)
{
	SDL_Rect a = r ? *r : (SDL_Rect){0, 0, (Uint16)dst->w, (Uint16)dst->h};
	for (int y = a.y; y < a.y + a.h && y < dst->h; y++)
		if (y >= 0)
			memset((uint8_t *)dst->pixels + y * dst->pitch + a.x,
			       (int)color, a.w);
	return 0;
}

// ---------------------------------------------------------------- stats HUD
//
// SDL_lite_stats(1) overlays "MS xx.x FPS yy" top-right on every present —
// the standard dev readout (ms averaged over 8 frames, display refreshed
// ~4x/s so it's readable). Rendered with a built-in 4x6 digit font in
// palette entry 255, which the HUD re-asserts to white on each present
// (games rarely miss it; the readout matters more while profiling).

void SDL_lite_stats(int enable) { stats_on = enable; }

static const uint8_t dig46[13][6] = {   // 4x6 glyphs: 0-9, '.', 'M','F'
	{7,5,5,5,5,7},{2,6,2,2,2,7},{7,1,7,4,4,7},{7,1,3,1,1,7},{5,5,7,1,1,1},
	{7,4,7,1,1,7},{7,4,7,5,5,7},{7,1,1,2,2,2},{7,5,7,5,5,7},{7,5,7,1,1,7},
	{0,0,0,0,0,2},{5,7,7,5,5,5},{7,4,6,4,4,4},
};

static void stats_glyph(uint8_t *fb, int fw, int x, int y, int g, uint8_t c)
{
	for (int r = 0; r < 6; r++)
		for (int b = 0; b < 3; b++)
			if (dig46[g][r] & (4 >> b))
				fb[(y + r) * fw + x + b] = c;
}

static void stats_render(uint8_t *fb, int fw, uint32_t frame_us)
{
	static uint32_t acc, n, shown_ms10, hold;
	acc += frame_us; n++;
	if (++hold >= 16) {                 // refresh readout ~4x/s at 60fps
		shown_ms10 = n ? acc / n / 100 : 0;   // ms x10
		acc = n = hold = 0;
	}
	uint32_t ms10 = shown_ms10 > 999 ? 999 : shown_ms10;
	uint32_t fps  = ms10 ? 10000 / ms10 : 0;
	if (fps > 99) fps = 99;
	// "M dd.d F dd" right-aligned box at top-right
	int x = fw - 46, y = 2;
	for (int i = 0; i < 44; i++)        // backdrop for contrast
		for (int r = 0; r < 8; r++)
			fb[(y - 1 + r) * fw + x - 1 + i] = 0;
	stats_glyph(fb, fw, x, y, 11, 255);            // 'M'
	stats_glyph(fb, fw, x + 5,  y, (ms10 / 100) % 10, 255);
	stats_glyph(fb, fw, x + 9,  y, (ms10 / 10) % 10, 255);
	stats_glyph(fb, fw, x + 13, y, 10, 255);       // '.'
	stats_glyph(fb, fw, x + 16, y, ms10 % 10, 255);
	stats_glyph(fb, fw, x + 24, y, 12, 255);       // 'F'
	stats_glyph(fb, fw, x + 29, y, (fps / 10) % 10, 255);
	stats_glyph(fb, fw, x + 33, y, fps % 10, 255);
}

static void stats_tick(uint8_t *fb, int fw)
{
	static uint32_t last_us;
	uint32_t now = sys_ticks_us();
	if (stats_on)
		stats_render(fb, fw, last_us ? now - last_us : 0);
	last_us = now;
}

// Fast path for ports that keep their OWN stable frame (Tyrian's VGAScreen):
// palette + pixels straight into the HAL back buffer — skips the shadow
// surface entirely, saving a full-screen memcpy per frame (~2 ms at 50 MHz).
void SDL_lite_present_indexed(const void *pixels, int pitch, int w, int h,
                              const void *colors256)
{
	if (colors256)
		SDL_SetColors(&screen, (const SDL_Color *)colors256, 0, 256);
	uint8_t *fb = fb_backbuffer();
	int fw = fb_width(), fh = fb_height();
	if (w > fw) w = fw;
	if (h > fh) h = fh;
	int ly = (fh - h) / 2, lx = (fw - w) / 2;
	static int bars_painted;                // both pages, then never again
	if (ly && bars_painted < 2) {
		memset(fb, 0, (size_t)fw * ly);
		memset(fb + (ly + h) * fw, 0, (size_t)fw * (fh - h - ly));
		bars_painted++;
	}
	uint8_t *dst = fb + ly * fw + lx;
	if (sys_caps()->features & HAL_FEAT_BLIT) {
		// hardware path: flush the source (CPU drew it), let the engine
		// copy at DRAM speed, overlay the HUD by CPU, flush only that
		flush_cpu_dcache_range((void *)pixels, (uint32_t)(h - 1) * pitch + w);
		blit(dst, pixels, (uint32_t)w, (uint32_t)h, (uint32_t)pitch,
		     (uint32_t)fw);
		blit_wait();
		if (stats_on) {
			stats_tick(fb + ly * fw, fw);
			flush_cpu_dcache_range(fb + ly * fw, (size_t)fw * 10);
		}
		SDL_lite_audio_pump();
		fb_present_dma();
	} else {
		const uint8_t *src = pixels;
		uint8_t *d = dst;
		for (int y = 0; y < h; y++, src += pitch, d += fw)
			memcpy(d, src, w);
		stats_tick(fb + ly * fw, fw);
		SDL_lite_audio_pump();
		fb_present();
	}
	ev_last_poll_us = 0;
}

// ---------------------------------------------------------------- events
//
// Pad bits become key events on their edges. Default map = OpenTyrian's
// default bindings.

static SDLKey keymap[16] = {
	SDLK_UP, SDLK_DOWN, SDLK_LEFT, SDLK_RIGHT,      // d-pad
	SDLK_LCTRL, SDLK_SPACE,                          // A=fire2? no: A,B
	SDLK_LALT,  SDLK_RETURN,                         // X,Y
	SDLK_s, SDLK_p,                                  // L1,R1
	SDLK_UNKNOWN, SDLK_UNKNOWN, SDLK_UNKNOWN, SDLK_UNKNOWN,
	SDLK_ESCAPE, SDLK_RETURN,                        // SELECT, START
};

static Uint8    keystate[SDLK_LAST];
static uint32_t pad_prev, pad_pend;

void SDL_lite_set_keymap(const SDLKey map[16])
{
	memcpy(keymap, map, sizeof(keymap));
}

int SDL_PollEvent(SDL_Event *ev)
{
	if (!pad_pend) {
		// Gate re-polls by TIME, not by flips: a game spinning in a
		// wait-for-release loop without flipping or delaying (Tyrian's
		// wait_noinput before demo playback) must still observe the
		// release, or it hangs forever (hardware v0.17.11: menu-launched
		// demo froze; attract-launched worked — no key was held).
		uint32_t now_us = sys_ticks_us();
		if (ev_last_poll_us && now_us - ev_last_poll_us < 4000)
			return 0;                   // ~4 ms: still one pass per frame
		input_poll();
		uint32_t now = input_buttons(0) & 0xFFFF;
		pad_pend = now ^ pad_prev;
		pad_prev = now;
		ev_last_poll_us = now_us ? now_us : 1;
		if (!pad_pend)
			return 0;
	}
	int bit = __builtin_ctz(pad_pend);
	pad_pend &= pad_pend - 1;
	SDLKey k = keymap[bit];
	if (k == SDLK_UNKNOWN || !ev)
		return SDL_PollEvent(ev);       // skip unmapped bits
	int down = (pad_prev >> bit) & 1;
	keystate[k] = (Uint8)down;
	ev->key.type       = down ? SDL_KEYDOWN : SDL_KEYUP;
	ev->key.keysym.sym = k;
	return 1;
}

Uint8 *SDL_GetKeyState(int *numkeys)
{
	if (numkeys)
		*numkeys = SDLK_LAST;
	return keystate;
}

// ---------------------------------------------------------------- time

Uint32 SDL_GetTicks(void)
{
	return sys_ticks_us() / 1000u;
}

void SDL_Delay(Uint32 ms)
{
	Uint32 t0 = SDL_GetTicks();
	while (SDL_GetTicks() - t0 < ms)
		SDL_lite_audio_pump();          // keep sound alive while waiting
	ev_last_poll_us = 0;                // a delay reopens the input gate
}

// ---------------------------------------------------------------- audio
//
// SDL's model is a callback filling a stream from an audio thread. We have
// no threads: SDL_lite_audio_pump() (called from Flip/Delay) asks the
// callback for exactly the frames the 48 kHz FIFO can absorb right now, so
// the callback experiences real-time pull just like under SDL.

static SDL_AudioSpec aspec;
static int           audio_on;

int SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained)
{
	aspec = *desired;
	aspec.freq = 48000;
	if (!aspec.samples || aspec.samples > 512)
		aspec.samples = 256;
	if (obtained)
		*obtained = aspec;
	return 0;
}

void SDL_PauseAudio(int pause_on) { audio_on = !pause_on; }
void SDL_CloseAudio(void)         { audio_on = 0; }

void SDL_lite_audio_pump(void)
{
	if (!audio_on || !aspec.callback)
		return;
	static int16_t buf[512 * 2];
	// NEVER BLOCK: ask the callback for exactly what the FIFO can absorb.
	// A blocking pump inside SDL_Delay(1) made every menu tick ~5 ms
	// (Tyrian jukebox crawl, hardware v0.17.9). Short pumps are fine —
	// the next Flip/Delay tops the FIFO up again.
	int frames = audio_stream_free();
	if (frames > (int)aspec.samples)
		frames = aspec.samples;
	frames &= ~1;
	if (frames < 16)
		return;                         // not worth a callback round trip
	int bytes = frames * (aspec.channels == 2 ? 4 : 2);
	aspec.callback(aspec.userdata, (Uint8 *)buf, bytes);
	if (aspec.channels == 1)
		for (int i = frames - 1; i >= 0; i--)
			buf[2 * i] = buf[2 * i + 1] = buf[i];
	audio_stream_write(buf, frames);
}

// ---------------------------------------------------------------- misc

int SDL_Init(Uint32 flags)
{
	(void)flags;                        // sys_init() is the game's job (crt0
	return 0;                           // convention: main calls it first)
}

void SDL_Quit(void)
{
	sys_exit();
}
