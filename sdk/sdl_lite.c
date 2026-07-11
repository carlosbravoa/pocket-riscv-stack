// sdl_lite — see sdl_lite.h. SDL 1.2's working set over the HAL.
// SPDX-License-Identifier: BSD-2-Clause
#include "sdl_lite.h"
#include "hal.h"
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
static uint32_t    ev_frame_done;   // input pass gate (events section below)

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
	SDL_lite_audio_pump();              // no interrupts: piggyback on vsync
	fb_present();
	ev_frame_done = 0;                  // a flip ends the input "frame" too
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
		if (ev_frame_done)
			return 0;                   // one poll pass per frame
		input_poll();
		uint32_t now = input_buttons(0) & 0xFFFF;
		pad_pend = now ^ pad_prev;
		pad_prev = now;
		ev_frame_done = 1;
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
	ev_frame_done = 0;                  // a delay ends the "frame"
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
	// mono callbacks fill the front half; we interleave to stereo in place
	int frames = aspec.samples;
	int bytes  = frames * (aspec.channels == 2 ? 4 : 2);
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
