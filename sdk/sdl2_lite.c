// sdl2_lite — see sdl2_lite.h. SDL2's working set over the HAL.
//
// Sits directly on hal.h (not on sdl_lite): the two shims serve different
// game shapes and never link into the same binary, so sharing plumbing would
// only tangle them. The pump/gate idioms are ported from sdl_lite.c, which
// paid for them on hardware.
//
// SPDX-License-Identifier: BSD-2-Clause
#include "sdl2_lite.h"
#include "hal.h"
#include "font8x8_basic.h"   // defines the glyph array: include in ONE .c only
#include <string.h>
#include <stdlib.h>

// ----------------------------------------------------------------- palette
//
// Entry 0 is reserved: transparent in surfaces/textures, black on screen.
// Draw colors are allocated on first use; overflow degrades to nearest.

static uint8_t pal[256][3];
static int     pal_n = 1;               // entry 0 reserved
static int     pal_dirty = 1;           // load our (black) palette on frame 1

static uint8_t pal_index(uint8_t r, uint8_t g, uint8_t b)
{
	for (int i = 1; i < pal_n; i++)
		if (pal[i][0] == r && pal[i][1] == g && pal[i][2] == b)
			return (uint8_t)i;
	if (pal_n < 256) {
		pal[pal_n][0] = r; pal[pal_n][1] = g; pal[pal_n][2] = b;
		pal_dirty = 1;
		return (uint8_t)pal_n++;
	}
	int best = 1;                       // full: nearest match (see sdl2_lite.h)
	uint32_t bd = 0xFFFFFFFFu;
	for (int i = 1; i < 256; i++) {
		int dr = pal[i][0] - r, dg = pal[i][1] - g, db = pal[i][2] - b;
		uint32_t d = (uint32_t)(dr * dr + dg * dg + db * db);
		if (d < bd) { bd = d; best = i; }
	}
	return (uint8_t)best;
}

// -------------------------------------------------------------- blending
//
// SDL_BLENDMODE_BLEND with a<255: src-over against the palette RGB behind
// each pixel, result re-quantized to 5 bits/channel (keeps alpha-heavy art
// from minting a fresh palette slot per pixel) and fed back through
// pal_index. The LUT below caches (current draw color, dst index) -> index,
// so fills cost one table lookup per pixel after first touch.

static uint8_t  blut[256];
static uint8_t  blut_valid[256];
static uint32_t blut_key = 0xFFFFFFFFu; // rgba the LUT was built for

static uint8_t blend_px(uint8_t dst, const uint8_t rgba[4])
{
	uint32_t key = ((uint32_t)rgba[0] << 24) | ((uint32_t)rgba[1] << 16) |
	               ((uint32_t)rgba[2] << 8)  | rgba[3];
	if (key != blut_key) {
		memset(blut_valid, 0, sizeof(blut_valid));
		blut_key = key;
	}
	if (!blut_valid[dst]) {
		unsigned a = rgba[3];
		unsigned r = (rgba[0] * a + pal[dst][0] * (255u - a)) / 255u;
		unsigned g = (rgba[1] * a + pal[dst][1] * (255u - a)) / 255u;
		unsigned b = (rgba[2] * a + pal[dst][2] * (255u - a)) / 255u;
		r = (r >= 248u) ? 248u : ((r + 4u) & 0xF8u);   // 5 bits/channel
		g = (g >= 248u) ? 248u : ((g + 4u) & 0xF8u);
		b = (b >= 248u) ? 248u : ((b + 4u) & 0xF8u);
		blut[dst] = pal_index((uint8_t)r, (uint8_t)g, (uint8_t)b);
		blut_valid[dst] = 1;
	}
	return blut[dst];
}

// ----------------------------------------------------------------- video

struct SDL_Window   { int w, h; };
struct SDL_Renderer { int unused; };
struct SDL_Texture  { int w, h; Uint8 *px; };

static struct SDL_Window   s_win;
static struct SDL_Renderer s_ren;
static uint8_t *canvas;                 // the game's w x h index buffer
static int      cw, ch, lx, ly;         // canvas size, letterbox offsets
static uint8_t  cur_rgba[4] = {0, 0, 0, 255};
static int      cur_idx = -1;           // lazy palette slot for cur_rgba
static int      cur_blend;              // SDL_BLENDMODE_* (draws only)
static uint32_t ev_last_poll_us;        // input gate (events section)

int SDL_CreateWindowAndRenderer(int w, int h, Uint32 flags,
                                SDL_Window **win, SDL_Renderer **ren)
{
	(void)flags;
	if (w <= 0 || h <= 0 || w > fb_width() || h > fb_height())
		return -1;                      // no scaler: the port fits or fails
	if (!canvas)
		canvas = malloc((size_t)w * h);
	if (!canvas)
		return -1;
	memset(canvas, 0, (size_t)w * h);
	cw = w; ch = h;
	lx = (fb_width() - w) / 2;
	ly = (fb_height() - h) / 2;
	s_win.w = w; s_win.h = h;
	if (win) *win = &s_win;
	if (ren) *ren = &s_ren;
	return 0;
}

void SDL_SetWindowTitle(SDL_Window *w, const char *title)
{
	(void)w; (void)title;               // the panel has no title bar
}
void SDL_DestroyWindow(SDL_Window *w)     { (void)w; }
void SDL_DestroyRenderer(SDL_Renderer *r) { (void)r; }

int SDL_SetRenderDrawColor(SDL_Renderer *r, Uint8 rd, Uint8 g, Uint8 b, Uint8 a)
{
	(void)r;
	cur_rgba[0] = rd; cur_rgba[1] = g; cur_rgba[2] = b; cur_rgba[3] = a;
	cur_idx = -1;                       // resolve on next draw
	return 0;
}

int SDL_GetRenderDrawColor(SDL_Renderer *r, Uint8 *rd, Uint8 *g, Uint8 *b, Uint8 *a)
{
	(void)r;
	if (rd) *rd = cur_rgba[0];
	if (g)  *g  = cur_rgba[1];
	if (b)  *b  = cur_rgba[2];
	if (a)  *a  = cur_rgba[3];
	return 0;
}

int SDL_SetRenderDrawBlendMode(SDL_Renderer *r, SDL_BlendMode mode)
{
	(void)r;
	cur_blend = (int)mode;
	return 0;
}

static uint8_t cur_index(void)
{
	if (cur_idx < 0)
		cur_idx = pal_index(cur_rgba[0], cur_rgba[1], cur_rgba[2]);
	return (uint8_t)cur_idx;
}

// 1 when the current draw must blend per-pixel (see sdl2_lite.h ALPHA)
static int cur_blending(void)
{
	return cur_blend == SDL_BLENDMODE_BLEND && cur_rgba[3] < 255;
}

int SDL_RenderClear(SDL_Renderer *r)
{
	(void)r;
	if (canvas)
		memset(canvas, cur_index(), (size_t)cw * ch);
	return 0;
}

int SDL_RenderFillRect(SDL_Renderer *r, const SDL_Rect *rect)
{
	(void)r;
	if (!canvas)
		return -1;
	SDL_Rect a = rect ? *rect : (SDL_Rect){0, 0, cw, ch};
	if (a.x < 0) { a.w += a.x; a.x = 0; }
	if (a.y < 0) { a.h += a.y; a.y = 0; }
	if (a.x + a.w > cw) a.w = cw - a.x;
	if (a.y + a.h > ch) a.h = ch - a.y;
	if (a.w <= 0 || a.h <= 0)
		return 0;
	uint8_t *row = canvas + (size_t)a.y * cw + a.x;
	if (cur_blending()) {
		if (cur_rgba[3] == 0)
			return 0;
		for (int y = 0; y < a.h; y++, row += cw)
			for (int x = 0; x < a.w; x++)
				row[x] = blend_px(row[x], cur_rgba);
		return 0;
	}
	uint8_t c = cur_index();
	for (int y = 0; y < a.h; y++, row += cw)
		memset(row, c, (size_t)a.w);
	return 0;
}

int SDL_RenderDrawPoint(SDL_Renderer *r, int x, int y)
{
	(void)r;
	if (!canvas || x < 0 || y < 0 || x >= cw || y >= ch)
		return 0;
	uint8_t *p = canvas + (size_t)y * cw + x;
	if (cur_blending()) {
		if (cur_rgba[3])
			*p = blend_px(*p, cur_rgba);
	} else {
		*p = cur_index();
	}
	return 0;
}

int SDL_RenderCopy(SDL_Renderer *r, SDL_Texture *t,
                   const SDL_Rect *srcrect, const SDL_Rect *dstrect)
{
	(void)r;
	if (!canvas || !t)
		return -1;
	int sx = srcrect ? srcrect->x : 0, sy = srcrect ? srcrect->y : 0;
	int w  = srcrect ? srcrect->w : t->w, h = srcrect ? srcrect->h : t->h;
	int dx = dstrect ? dstrect->x : 0, dy = dstrect ? dstrect->y : 0;
	// unscaled: dstrect w/h ignored (documented in sdl2_lite.h)
	if (sx < 0) { w += sx; dx -= sx; sx = 0; }
	if (sy < 0) { h += sy; dy -= sy; sy = 0; }
	if (sx + w > t->w) w = t->w - sx;
	if (sy + h > t->h) h = t->h - sy;
	if (dx < 0) { w += dx; sx -= dx; dx = 0; }
	if (dy < 0) { h += dy; sy -= dy; dy = 0; }
	if (dx + w > cw) w = cw - dx;
	if (dy + h > ch) h = ch - dy;
	for (int y = 0; y < h; y++) {
		const Uint8 *s = t->px + (size_t)(sy + y) * t->w + sx;
		uint8_t     *d = canvas + (size_t)(dy + y) * cw + dx;
		for (int x = 0; x < w; x++)
			if (s[x])                   // 0 = transparent
				d[x] = s[x];
	}
	return 0;
}

void SDL_RenderPresent(SDL_Renderer *r)
{
	(void)r;
	if (!canvas)
		return;
	RVSDL2_AudioPump();                 // no interrupts: piggyback on presents
	uint8_t *fb = fb_backbuffer();
	int fw = fb_width(), fh = fb_height();
	static int bars_painted;            // both pages once, then never again
	if ((lx || ly) && bars_painted < 2) {
		memset(fb, 0, (size_t)fw * fh);
		bars_painted++;
	}
	const uint8_t *src = canvas;
	uint8_t *dst = fb + (size_t)ly * fw + lx;
	for (int y = 0; y < ch; y++, src += cw, dst += fw)
		memcpy(dst, src, (size_t)cw);
	if (pal_dirty) {
		palette_set(pal);
		pal_dirty = 0;
	}
	fb_present();
	ev_last_poll_us = 0;                // a present reopens the input gate
}

// ----------------------------------------------------------- surfaces/tex

static SDL_Surface *surface_new(int w, int h)
{
	SDL_Surface *s = malloc(sizeof(*s));
	if (!s)
		return 0;
	s->pixels = calloc(1, (size_t)w * h);
	if (!s->pixels) {
		free(s);
		return 0;
	}
	s->w = w; s->h = h; s->pitch = w;
	return s;
}

void SDL_FreeSurface(SDL_Surface *s)
{
	if (!s)
		return;
	free(s->pixels);
	free(s);
}

SDL_Texture *SDL_CreateTextureFromSurface(SDL_Renderer *r, SDL_Surface *s)
{
	(void)r;
	if (!s)
		return 0;
	SDL_Texture *t = malloc(sizeof(*t));
	if (!t)
		return 0;
	t->px = malloc((size_t)s->w * s->h);
	if (!t->px) {
		free(t);
		return 0;
	}
	for (int y = 0; y < s->h; y++)
		memcpy(t->px + (size_t)y * s->w,
		       s->pixels + (size_t)y * s->pitch, (size_t)s->w);
	t->w = s->w; t->h = s->h;
	return t;
}

int SDL_QueryTexture(SDL_Texture *t, Uint32 *format, int *access, int *w, int *h)
{
	if (!t)
		return -1;
	if (format) *format = 0;
	if (access) *access = 0;
	if (w) *w = t->w;
	if (h) *h = t->h;
	return 0;
}

void SDL_DestroyTexture(SDL_Texture *t)
{
	if (!t)
		return;
	free(t->px);
	free(t);
}

// ------------------------------------------------------------------ events
//
// Pad bits become key events on their edges (idiom from sdl_lite.c: the
// time-gated input_poll keeps spin-polling loops honest without re-sampling
// the pad more than once per frame).

static Uint16 padmap[16] = {
	SDL_SCANCODE_UP, SDL_SCANCODE_DOWN,           // d-pad
	SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
	SDL_SCANCODE_X, SDL_SCANCODE_Z,               // A, B (rotate CW/CCW)
	SDL_SCANCODE_SPACE, SDL_SCANCODE_LSHIFT,      // X, Y (drop, hold)
	SDL_SCANCODE_LSHIFT, SDL_SCANCODE_SPACE,      // L1, R1
	0, 0, 0, 0,
	SDL_SCANCODE_ESCAPE, SDL_SCANCODE_RETURN,     // SELECT, START
};

static Uint8    keystate[SDL_NUM_SCANCODES];
static uint32_t pad_prev, pad_pend;
static int      quit_posted;

void RVSDL2_SetPadMap(const Uint16 map[16])
{
	memcpy(padmap, map, sizeof(padmap));
}

int SDL_PollEvent(SDL_Event *ev)
{
	fb_flip_poll();                     // deferred flip: event-driven redraws
	                                    // reach the screen (see hal.h)
	if (!pad_pend) {
		uint32_t now_us = sys_ticks_us();
		if (ev_last_poll_us && now_us - ev_last_poll_us < 4000)
			return 0;                   // ~4 ms: still one pad pass per frame
		input_poll();
		uint32_t now = input_buttons(0) & 0xFFFF;
		pad_pend = now ^ pad_prev;
		pad_prev = now;
		ev_last_poll_us = now_us ? now_us : 1;
		const uint32_t both = HAL_BTN_SELECT | HAL_BTN_START;
		if ((now & both) == both && !quit_posted) {
			quit_posted = 1;            // console convention: SELECT+START
			if (ev) {
				memset(ev, 0, sizeof(*ev));
				ev->type = SDL_QUIT;
			}
			return 1;
		}
		if (!pad_pend)
			return 0;
	}
	int bit = __builtin_ctz(pad_pend);
	pad_pend &= pad_pend - 1;
	Uint16 sc = padmap[bit];
	if (!sc || !ev)
		return SDL_PollEvent(ev);       // skip unmapped bits
	int down = (pad_prev >> bit) & 1;
	keystate[sc] = (Uint8)down;
	memset(ev, 0, sizeof(*ev));
	ev->type              = down ? SDL_KEYDOWN : SDL_KEYUP;
	ev->key.state         = down ? SDL_PRESSED : SDL_RELEASED;
	ev->key.keysym.scancode = (SDL_Scancode)sc;
	ev->key.keysym.sym    = sc;         // no keymap layer: sym mirrors scancode
	return 1;
}

const Uint8 *SDL_GetKeyboardState(int *numkeys)
{
	if (numkeys)
		*numkeys = SDL_NUM_SCANCODES;
	return keystate;
}

// -------------------------------------------------------------------- time

Uint32 SDL_GetTicks(void)
{
	return sys_ticks_us() / 1000u;
}

void SDL_Delay(Uint32 ms)
{
	Uint32 t0 = SDL_GetTicks();
	while (SDL_GetTicks() - t0 < ms) {
		fb_flip_poll();                 // keep deferred flips moving
		RVSDL2_AudioPump();             // ... and the FIFO topped up
	}
	ev_last_poll_us = 0;                // a delay reopens the input gate
}

// -------------------------------------------------------------------- misc

int SDL_Init(Uint32 flags)
{
	(void)flags;
	sys_init();                         // idempotent on both HALs
	return 0;
}

int SDL_InitSubSystem(Uint32 flags)
{
	(void)flags;                        // audio opens in Mix_OpenAudio; the
	return 0;                           // rest is always on
}

void SDL_Quit(void)
{
	sys_exit();
}

const char *SDL_GetError(void)
{
	return "";
}

int SDL_SetHint(const char *name, const char *value)
{
	(void)name; (void)value;            // no hints to take (documented)
	return 1;
}

// ------------------------------------------------- SDL2_ttf (bitmap font)
//
// See sdl2_lite.h: the font file is ignored, glyphs come from the SDK's
// public-domain 8x8 bitmap font, integer-scaled by ptsize (8 -> 1x ...
// 32+ -> 4x). Row bits are LSB = leftmost pixel.

struct TTF_Font_s { int ptsize; int scale; };
#define TTF_POOL 12                     // distinct ptsizes a game may open
static struct TTF_Font_s font_pool[TTF_POOL];
static int font_pool_n;

int         TTF_Init(void)     { return 0; }
void        TTF_Quit(void)     {}
const char *TTF_GetError(void) { return ""; }

TTF_Font *TTF_OpenFont(const char *file, int ptsize)
{
	(void)file;                         // built-in font only (documented)
	int scale = ptsize / 8;
	if (scale < 1) scale = 1;
	if (scale > 4) scale = 4;
	for (int i = 0; i < font_pool_n; i++)
		if (font_pool[i].ptsize == ptsize)
			return &font_pool[i];
	if (font_pool_n == TTF_POOL)        // pool full: nearest habit is fine
		return &font_pool[0];
	font_pool[font_pool_n].ptsize = ptsize;
	font_pool[font_pool_n].scale  = scale;
	return &font_pool[font_pool_n++];
}

void TTF_CloseFont(TTF_Font *f) { (void)f; }

int TTF_FontHeight(const TTF_Font *f)
{
	return f ? 8 * f->scale : 8;
}

SDL_Surface *TTF_RenderUTF8_Blended(TTF_Font *f, const char *text,
                                    SDL_Color color)
{
	int sc = (f && f->scale > 0) ? f->scale : 1;
	size_t n = text ? strlen(text) : 0;
	if (!n)
		return 0;                       // real SDL_ttf also errors on ""
	SDL_Surface *s = surface_new((int)n * 8 * sc, 8 * sc);
	if (!s)
		return 0;
	uint8_t ci = pal_index(color.r, color.g, color.b);
	for (size_t i = 0; i < n; i++) {
		unsigned c = (unsigned char)text[i];
		if (c > 127)
			continue;                   // ASCII only: high bytes stay blank
		const char *g = font8x8_basic[c];
		for (int ry = 0; ry < 8; ry++) {
			for (int rx = 0; rx < 8; rx++) {
				if (!((g[ry] >> rx) & 1))
					continue;
				uint8_t *d = s->pixels + (size_t)(ry * sc) * s->pitch +
				             ((int)i * 8 + rx) * sc;
				for (int yy = 0; yy < sc; yy++, d += s->pitch)
					for (int xx = 0; xx < sc; xx++)
						d[xx] = ci;
			}
		}
	}
	return s;
}

// --------------------------------------------- SDL2_mixer (pump model)
//
// See sdl2_lite.h MIXER: chunks are mono s16 @ 48 kHz (the asset pipeline's
// contract), mixed with saturation on 8 one-shot channels into the HAL
// stream. The pump only ever writes what audio_stream_free() reports — the
// non-blocking discipline SDL_lite_audio_pump established (a blocking pump
// inside a delay loop made every Tyrian menu tick ~5 ms, v0.17.9).

#define MIX_CHANNELS 8
static struct {
	const int16_t *pcm;                 // NULL = free
	uint32_t pos, len;                  // in samples
} mixch[MIX_CHANNELS];
static int mix_open;

int Mix_OpenAudio(int frequency, Uint16 format, int channels, int chunksize)
{
	(void)frequency; (void)format; (void)channels; (void)chunksize;
	if (audio_stream_open(48000) < 0)
		return -1;
	mix_open = 1;
	return 0;
}

int Mix_AllocateChannels(int numchans)
{
	(void)numchans;                     // fixed pool (documented)
	return MIX_CHANNELS;
}

Mix_Chunk *Mix_QuickLoad_RAW(Uint8 *mem, Uint32 len)
{
	if (!mem || len < 2)
		return 0;
	Mix_Chunk *c = malloc(sizeof(*c));
	if (!c)
		return 0;
	c->abuf = mem;
	c->alen = len & ~1u;                // whole samples
	c->allocated = 0;                   // caller owns the bytes
	return c;
}

int Mix_PlayChannel(int channel, Mix_Chunk *chunk, int loops)
{
	(void)loops;                        // one-shots only (documented)
	if (!mix_open || !chunk || !chunk->abuf)
		return -1;
	if (channel < 0 || channel >= MIX_CHANNELS) {
		for (channel = 0; channel < MIX_CHANNELS && mixch[channel].pcm;
		     channel++)
			;
		if (channel == MIX_CHANNELS)
			return -1;                  // all busy: drop, like a full mixer
	}
	mixch[channel].pcm = (const int16_t *)(void *)chunk->abuf;
	mixch[channel].pos = 0;
	mixch[channel].len = chunk->alen / 2;
	return channel;
}

void Mix_FreeChunk(Mix_Chunk *chunk)
{
	if (!chunk)
		return;
	for (int i = 0; i < MIX_CHANNELS; i++)
		if (mixch[i].pcm == (const int16_t *)(void *)chunk->abuf)
			mixch[i].pcm = 0;
	free(chunk);                        // abuf stays with its owner
}

void Mix_CloseAudio(void)
{
	for (int i = 0; i < MIX_CHANNELS; i++)
		mixch[i].pcm = 0;
	mix_open = 0;
}

const char *Mix_GetError(void)
{
	return "";
}

void RVSDL2_AudioPump(void)
{
	if (!mix_open)
		return;
	static int16_t buf[512 * 2];
	// NEVER BLOCK: write exactly what the FIFO can absorb, in chunks, until
	// it is (nearly) full — silence included, so latency stays flat.
	for (;;) {
		int frames = audio_stream_free();
		if (frames > 512)
			frames = 512;
		if (frames < 16)
			return;                     // FIFO full (or nearly): done
		for (int i = 0; i < frames; i++) {
			int32_t acc = 0;
			for (int v = 0; v < MIX_CHANNELS; v++) {
				if (!mixch[v].pcm)
					continue;
				acc += mixch[v].pcm[mixch[v].pos++];
				if (mixch[v].pos >= mixch[v].len)
					mixch[v].pcm = 0;   // one-shot done
			}
			if (acc >  32767) acc =  32767;
			if (acc < -32768) acc = -32768;
			buf[2 * i] = buf[2 * i + 1] = (int16_t)acc;
		}
		audio_stream_write(buf, frames);
	}
}
