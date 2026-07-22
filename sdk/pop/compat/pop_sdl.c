/*
 * pop_sdl.c — the SDL2 surface/event/timing shim for SDLPoP on RISC-V Stack.
 * Implements every psdl_* declared in compat/SDL.h in software: truecolor +
 * 8bpp-indexed surfaces, palette-aware blits, the event queue (HAL pad ->
 * synthesized SDL2 controller events), timing, and the quantize+present path
 * (pop_present) that turns PoP's final 24bpp surface into 8bpp indices for the
 * hardware palette. See PORTING.md.
 *
 * SPDX-License-Identifier: BSD-2-Clause (shim); game is GPL-3.0-or-later.
 */
#include "SDL.h"                 /* our shadow header */
#include "hal.h"                 /* pad, ticks, exit */
#include "rv_bridge.h"           /* present + audio (via lite_bridge) */

#include <stdlib.h>
#include <string.h>

/* ============================ surfaces ============================ */

static SDL_PixelFormat *make_format(int depth)
{
	SDL_PixelFormat *f = calloc(1, sizeof *f);
	f->BitsPerPixel  = (Uint8)depth;
	f->BytesPerPixel = (Uint8)((depth + 7) / 8);
	if (depth == 8) {
		f->format  = SDL_PIXELFORMAT_INDEX8;
		f->palette = calloc(1, sizeof *f->palette);
		f->palette->ncolors = 256;
		f->palette->colors  = calloc(256, sizeof(SDL_Color));
		f->palette->refcount = 1;
	} else if (depth == 24) {
		f->format = SDL_PIXELFORMAT_RGB24;
		f->Rmask = 0xFF0000; f->Gmask = 0x00FF00; f->Bmask = 0x0000FF;
	} else { /* 32 */
		f->format = SDL_PIXELFORMAT_ARGB8888;
		f->Rmask = 0xFF0000; f->Gmask = 0x00FF00; f->Bmask = 0x0000FF;
		f->Amask = 0xFF000000u;
	}
	return f;
}

SDL_Surface *psdl_CreateRGBSurface(Uint32 flags, int w, int h, int depth,
	Uint32 rm, Uint32 gm, Uint32 bm, Uint32 am)
{
	(void)flags; (void)rm; (void)gm; (void)bm; (void)am;
	SDL_Surface *s = calloc(1, sizeof *s);
	s->format = make_format(depth);
	s->w = w; s->h = h;
	s->pitch = w * s->format->BytesPerPixel;
	s->pixels = calloc((size_t)s->pitch * h, 1);
	s->clip_rect = (SDL_Rect){0, 0, w, h};
	s->colorkey = 0; s->alphamod = 255; s->blendmode = SDL_BLENDMODE_NONE;
	s->refcount = 1;
	return s;
}

SDL_Surface *psdl_CreateRGBSurfaceWithFormat(Uint32 flags, int w, int h,
	int depth, Uint32 format)
{
	SDL_Surface *s = psdl_CreateRGBSurface(flags, w, h, depth, 0, 0, 0, 0);
	if (format) s->format->format = format;
	return s;
}

void psdl_FreeSurface(SDL_Surface *s)
{
	if (!s) return;
	if (s->format) {
		SDL_Palette *pal = s->format->palette;
		if (pal && --pal->refcount <= 0) {   // refcounted: hflip shares palettes
			free(pal->colors);
			free(pal);
		}
		free(s->format);
	}
	free(s->pixels);
	free(s);
}

int psdl_LockSurface(SDL_Surface *s)   { (void)s; return 0; }
void psdl_UnlockSurface(SDL_Surface *s) { (void)s; }

int psdl_SetColorKey(SDL_Surface *s, int flag, Uint32 key)
{
	if (!s) return -1;
	if (flag) { s->flags |= SDL_SRCCOLORKEY; s->colorkey = key; }
	else        s->flags &= ~SDL_SRCCOLORKEY;
	return 0;
}
int psdl_SetSurfaceBlendMode(SDL_Surface *s, int mode) { if (s) s->blendmode = mode; return 0; }
int psdl_SetSurfaceAlphaMod(SDL_Surface *s, Uint8 a)   { if (s) s->alphamod = a; return 0; }
int psdl_SetAlpha(SDL_Surface *s, Uint32 flag, Uint8 a)
{
	if (!s) return -1;
	if (flag & SDL_SRCALPHA) { s->blendmode = SDL_BLENDMODE_BLEND; s->alphamod = a; }
	else s->blendmode = SDL_BLENDMODE_NONE;
	return 0;
}
int psdl_SetClipRect(SDL_Surface *s, const SDL_Rect *r)
{
	if (!s) return 0;
	if (r) s->clip_rect = *r; else s->clip_rect = (SDL_Rect){0,0,s->w,s->h};
	return 1;
}

Uint32 psdl_MapRGB(const SDL_PixelFormat *f, Uint8 r, Uint8 g, Uint8 b)
{ (void)f; return ((Uint32)r << 16) | ((Uint32)g << 8) | b; }
Uint32 psdl_MapRGBA(const SDL_PixelFormat *f, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{ (void)f; return ((Uint32)a << 24) | ((Uint32)r << 16) | ((Uint32)g << 8) | b; }

int psdl_SetPaletteColors(SDL_Palette *pal, const SDL_Color *colors, int first, int n)
{
	if (!pal || !pal->colors) return -1;
	for (int i = 0; i < n; i++) {
		int d = first + i;
		if (d >= 0 && d < pal->ncolors) pal->colors[d] = colors[i];
	}
	return 0;
}
int psdl_SetSurfacePalette(SDL_Surface *s, SDL_Palette *pal)
{
	if (s && s->format) {
		SDL_Palette *old = s->format->palette;
		if (old && old != pal && --old->refcount <= 0) {
			free(old->colors); free(old);
		}
		s->format->palette = pal;
		if (pal) pal->refcount++;
	}
	return 0;
}
int psdl_SetColors(SDL_Surface *s, SDL_Color *colors, int first, int n)
{
	if (s && s->format && s->format->palette)
		return psdl_SetPaletteColors(s->format->palette, colors, first, n) == 0;
	return 0;
}

/* Resolve a source pixel to R,G,B given the source surface's format. */
static inline void src_rgb(const SDL_Surface *s, const uint8_t *p,
	uint8_t *r, uint8_t *g, uint8_t *b)
{
	if (s->format->BytesPerPixel == 1) {
		if (!s->format->palette) { *r = *g = *b = 0; return; }
		SDL_Color c = s->format->palette->colors[*p];
		*r = c.r; *g = c.g; *b = c.b;
	} else if (s->format->BytesPerPixel == 3) {
		*r = p[0]; *g = p[1]; *b = p[2];
	} else { /* 4: ARGB stored as [B,G,R,A] little-endian of 0xAARRGGBB? we
	            store R,G,B,A */
		*r = p[0]; *g = p[1]; *b = p[2];
	}
}

int psdl_BlitSurface(SDL_Surface *src, const SDL_Rect *srect,
	SDL_Surface *dst, SDL_Rect *drect)
{
	if (!src || !dst) return -1;
	int sx = srect ? srect->x : 0, sy = srect ? srect->y : 0;
	int sw = srect ? srect->w : src->w, sh = srect ? srect->h : src->h;
	int dx = drect ? drect->x : 0, dy = drect ? drect->y : 0;

	/* clip to dst->clip_rect */
	SDL_Rect cl = dst->clip_rect;
	if (dx < cl.x) { int o = cl.x - dx; sx += o; sw -= o; dx = cl.x; }
	if (dy < cl.y) { int o = cl.y - dy; sy += o; sh -= o; dy = cl.y; }
	if (dx + sw > cl.x + cl.w) sw = cl.x + cl.w - dx;
	if (dy + sh > cl.y + cl.h) sh = cl.y + cl.h - dy;
	if (sx < 0) { dx -= sx; sw += sx; sx = 0; }
	if (sy < 0) { dy -= sy; sh += sy; sy = 0; }
	if (sx + sw > src->w) sw = src->w - sx;
	if (sy + sh > src->h) sh = src->h - sy;
	if (sw <= 0 || sh <= 0) return 0;
	if (drect) { drect->w = sw; drect->h = sh; }

	int has_ck = (src->flags & SDL_SRCCOLORKEY) != 0;
	Uint32 ck = src->colorkey;
	int blend = src->blendmode == SDL_BLENDMODE_BLEND;
	Uint8 am = src->alphamod;
	int dbpp = dst->format->BytesPerPixel;
	int sbpp = src->format->BytesPerPixel;

	for (int y = 0; y < sh; y++) {
		const uint8_t *sp = (const uint8_t *)src->pixels + (size_t)(sy+y)*src->pitch + (size_t)sx*sbpp;
		uint8_t *dp = (uint8_t *)dst->pixels + (size_t)(dy+y)*dst->pitch + (size_t)dx*dbpp;
		for (int x = 0; x < sw; x++, sp += sbpp, dp += dbpp) {
			if (has_ck && sbpp == 1 && *sp == ck) continue;
			if (dbpp == 1) {
				/* index dst: raw index copy (same-palette 8->8), no palette
				 * lookup needed — this is hflip's copy path */
				*dp = (sbpp == 1) ? *sp : 0;
				continue;
			}
			uint8_t r, g, b; src_rgb(src, sp, &r, &g, &b);
			{
				if (blend && am < 255) {
					dp[0] = (uint8_t)((r*am + dp[0]*(255-am))/255);
					dp[1] = (uint8_t)((g*am + dp[1]*(255-am))/255);
					dp[2] = (uint8_t)((b*am + dp[2]*(255-am))/255);
				} else { dp[0] = r; dp[1] = g; dp[2] = b; }
				if (dbpp == 4) dp[3] = 255;
			}
		}
	}
	return 0;
}

int psdl_BlitScaled(SDL_Surface *src, const SDL_Rect *sr, SDL_Surface *dst, SDL_Rect *dr)
{ return psdl_BlitSurface(src, sr, dst, dr); }  /* PoP only 1:1 here */

int psdl_FillRect(SDL_Surface *dst, const SDL_Rect *rect, Uint32 color)
{
	if (!dst) return -1;
	int x0 = rect ? rect->x : 0, y0 = rect ? rect->y : 0;
	int w = rect ? rect->w : dst->w, h = rect ? rect->h : dst->h;
	SDL_Rect cl = dst->clip_rect;
	if (x0 < cl.x) { w -= cl.x - x0; x0 = cl.x; }
	if (y0 < cl.y) { h -= cl.y - y0; y0 = cl.y; }
	if (x0 + w > cl.x + cl.w) w = cl.x + cl.w - x0;
	if (y0 + h > cl.y + cl.h) h = cl.y + cl.h - y0;
	if (w <= 0 || h <= 0) return 0;
	int bpp = dst->format->BytesPerPixel;
	uint8_t r = (color>>16)&0xFF, g = (color>>8)&0xFF, b = color&0xFF;
	for (int y = 0; y < h; y++) {
		uint8_t *dp = (uint8_t *)dst->pixels + (size_t)(y0+y)*dst->pitch + (size_t)x0*bpp;
		for (int x = 0; x < w; x++, dp += bpp) {
			if (bpp == 1) *dp = (uint8_t)color;
			else { dp[0]=r; dp[1]=g; dp[2]=b; if (bpp==4) dp[3]=(color>>24)&0xFF; }
		}
	}
	return 0;
}

SDL_Surface *psdl_ConvertSurface(SDL_Surface *s, const SDL_PixelFormat *fmt, Uint32 flags)
{
	(void)flags;
	int depth = fmt ? fmt->BitsPerPixel : 24;
	SDL_Surface *out = psdl_CreateRGBSurface(0, s->w, s->h, depth, 0,0,0,0);
	psdl_BlitSurface(s, 0, out, 0);
	return out;
}
SDL_Surface *psdl_ConvertSurfaceFormat(SDL_Surface *s, Uint32 fmt, Uint32 flags)
{
	(void)flags;
	int depth = (fmt == SDL_PIXELFORMAT_ARGB8888) ? 32
	          : (fmt == SDL_PIXELFORMAT_INDEX8)   ? 8 : 24;
	SDL_Surface *out = psdl_CreateRGBSurface(0, s->w, s->h, depth, 0,0,0,0);
	out->format->format = fmt ? fmt : out->format->format;
	psdl_BlitSurface(s, 0, out, 0);
	return out;
}

/* ============================ present (quantize) ============================ */

static int16_t rev_lut[32768];     /* RGB555 -> palette index (-1 = uncomputed) */
static uint8_t lut_pal[256][3];
static int     lut_valid;

static inline int rgb555(int r, int g, int b)
{ return ((r>>3)<<10) | ((g>>3)<<5) | (b>>3); }

static int nearest(const uint8_t pal[256][3], int r, int g, int b)
{
	int best = 0, bestd = 1<<30;
	for (int i = 0; i < 256; i++) {
		int dr = pal[i][0]-r, dg = pal[i][1]-g, db = pal[i][2]-b;
		int d = dr*dr + dg*dg + db*db;
		if (d < bestd) { bestd = d; best = i; if (!d) break; }
	}
	return best;
}

static void lut_rebuild(const uint8_t pal[256][3])
{
	memcpy(lut_pal, pal, sizeof lut_pal);
	for (int i = 0; i < 32768; i++) rev_lut[i] = -1;
	for (int i = 255; i >= 0; i--)           /* low index wins ties */
		rev_lut[rgb555(pal[i][0], pal[i][1], pal[i][2])] = (int16_t)i;
	lut_valid = 1;
}

/* Turn PoP's final 24bpp surface into 8bpp indices against pal[256][3] and
 * present through the blitter. Exact palette colors hit the LUT; blended
 * off-palette pixels take a one-time nearest-match, cached in the LUT. */
void pop_present(const uint8_t *rgb, int pitch, int w, int h,
	const uint8_t pal[256][3])
{
	static uint8_t idxbuf[320*200];
	static uint8_t pal_rgbx[256][4];      // sdl_lite casts colors256 to
	                                      // SDL_Color (r,g,b,unused) — 4-byte
	if (!lut_valid || memcmp(lut_pal, pal, sizeof lut_pal) != 0) {
		lut_rebuild(pal);
		for (int i = 0; i < 256; i++) {
			pal_rgbx[i][0] = pal[i][0]; pal_rgbx[i][1] = pal[i][1];
			pal_rgbx[i][2] = pal[i][2]; pal_rgbx[i][3] = 0;
		}
	}
	if (w > 320) w = 320; if (h > 200) h = 200;
	for (int y = 0; y < h; y++) {
		const uint8_t *sp = rgb + (size_t)y*pitch;
		uint8_t *dp = idxbuf + (size_t)y*320;
		for (int x = 0; x < w; x++, sp += 3) {
			int k = rgb555(sp[0], sp[1], sp[2]);
			int idx = rev_lut[k];
			if (idx < 0) { idx = nearest(pal, sp[0], sp[1], sp[2]); rev_lut[k] = (int16_t)idx; }
			dp[x] = (uint8_t)idx;
		}
	}
	rvb_present_indexed(idxbuf, 320, w, h, pal_rgbx);
}

/* ============================ events / input ============================ */

static SDL_Event evq[128];
static int evq_head, evq_tail;
static uint32_t prev_pad;
static int pad_inited;

static void evq_push(const SDL_Event *e)
{
	int n = (evq_tail + 1) & 127;
	if (n == evq_head) return;             /* full: drop */
	evq[evq_tail] = *e; evq_tail = n;
}
int psdl_PushEvent(SDL_Event *ev) { evq_push(ev); return 1; }

/* HAL pad bit -> (controller button, menu scancode-or-0) */
struct padmap { uint32_t bit; int cbtn; int sc; };
static const struct padmap PADMAP[] = {
	{ HAL_BTN_UP,     SDL_CONTROLLER_BUTTON_DPAD_UP,    SDL_SCANCODE_UP },
	{ HAL_BTN_DOWN,   SDL_CONTROLLER_BUTTON_DPAD_DOWN,  SDL_SCANCODE_DOWN },
	{ HAL_BTN_LEFT,   SDL_CONTROLLER_BUTTON_DPAD_LEFT,  SDL_SCANCODE_LEFT },
	{ HAL_BTN_RIGHT,  SDL_CONTROLLER_BUTTON_DPAD_RIGHT, SDL_SCANCODE_RIGHT },
	{ HAL_BTN_A,      SDL_CONTROLLER_BUTTON_A,          0 },   /* down/careful */
	{ HAL_BTN_B,      SDL_CONTROLLER_BUTTON_B,          0 },
	{ HAL_BTN_X,      SDL_CONTROLLER_BUTTON_X,          0 },   /* shift */
	{ HAL_BTN_Y,      SDL_CONTROLLER_BUTTON_Y,          0 },   /* up/jump */
	{ HAL_BTN_START,  SDL_CONTROLLER_BUTTON_START,      SDL_SCANCODE_RETURN },
	{ HAL_BTN_SELECT, SDL_CONTROLLER_BUTTON_BACK,       SDL_SCANCODE_BACKSPACE },
};

static void sample_pad(void)
{
	input_poll();
	uint32_t b = input_buttons(0);
	if (!pad_inited) { prev_pad = b; pad_inited = 1; }  /* no spurious edge at boot */
	uint32_t changed = b ^ prev_pad;
	for (unsigned i = 0; i < sizeof PADMAP/sizeof PADMAP[0]; i++) {
		if (!(changed & PADMAP[i].bit)) continue;
		int down = (b & PADMAP[i].bit) != 0;
		SDL_Event e; memset(&e, 0, sizeof e);
		e.type = down ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP;
		e.cbutton.button = (Uint8)PADMAP[i].cbtn;
		e.cbutton.state = down;
		evq_push(&e);
		if (PADMAP[i].sc) {           /* also a key event for menu paths */
			SDL_Event k; memset(&k, 0, sizeof k);
			k.type = down ? SDL_KEYDOWN : SDL_KEYUP;
			k.key.keysym.scancode = PADMAP[i].sc;
			k.key.state = down;
			evq_push(&k);
		}
	}
	prev_pad = b;
	/* SELECT+START held together = quit to picker (SDK convention). */
	if ((b & HAL_BTN_SELECT) && (b & HAL_BTN_START))
		sys_exit();
}

int psdl_PollEvent(SDL_Event *ev)
{
	if (evq_head == evq_tail) sample_pad();
	if (evq_head == evq_tail) return 0;
	if (ev) *ev = evq[evq_head];
	evq_head = (evq_head + 1) & 127;
	return 1;
}

static Uint8 keystate[SDL_NUM_SCANCODES];
const Uint8 *psdl_GetKeyboardState(int *n) { if (n) *n = SDL_NUM_SCANCODES; return keystate; }
Uint32 psdl_GetMouseState(int *x, int *y) { if (x) *x = 0; if (y) *y = 0; return 0; }

/* ============================ timing ============================ */

Uint32 psdl_GetTicks(void)   { return sys_ticks_us() / 1000u; }
Uint64 psdl_GetPerformanceCounter(void)   { return sys_ticks_us(); }
Uint64 psdl_GetPerformanceFrequency(void) { return 1000000u; }
void   psdl_Delay(Uint32 ms)
{
	uint32_t t0 = sys_ticks_us();
	do { rvb_audio_pump(); } while (sys_ticks_us() - t0 < ms * 1000u);
}

/* ============================ audio (via bridge) ============================ */

static SDL_AudioCallback au_cb; static void *au_ud; static int au_open;
int psdl_OpenAudio(SDL_AudioSpec *want, SDL_AudioSpec *got)
{
	if (!want) return -1;
	if (getenv("RVSTACK_NOAUDIO")) return -1;   // headless render tests: the
	                                            // dummy audio device doesn't
	                                            // drain, deadlocking the pump
	au_cb = want->callback; au_ud = want->userdata;
	if (got) { *got = *want; got->freq = 48000; got->format = AUDIO_S16SYS; }
	if (rvb_audio_open(want->channels, want->samples ? want->samples : 512,
	                   au_cb, au_ud) < 0) return -1;
	au_open = 1;
	return 0;
}
void psdl_CloseAudio(void) { if (au_open) rvb_audio_close(); au_open = 0; }
void psdl_PauseAudio(int p) { if (au_open) rvb_audio_pause(p); }
void psdl_LockAudio(void)   {}
void psdl_UnlockAudio(void) {}
int  psdl_GetAudioStatus(void) { return au_open ? SDL_AUDIO_PLAYING : SDL_AUDIO_STOPPED; }

/* ============================ misc no-ops ============================ */

int  psdl_Init(Uint32 f)          { (void)f; rvb_video_init(); return 0; }
int  psdl_InitSubSystem(Uint32 f) { (void)f; return 0; }
void psdl_Quit(void)              { sys_exit(); }
int  psdl_ShowCursor(int t)       { (void)t; return 0; }
int  psdl_ShowSimpleMessageBox(Uint32 f, const char *t, const char *m, SDL_Window *w)
{ (void)f;(void)t;(void)m;(void)w; return 0; }
SDL_bool psdl_SetHint(const char *n, const char *v) { (void)n;(void)v; return SDL_TRUE; }
static const char *g_err = "";
const char *psdl_GetError(void) { return g_err; }
const char *psdl_GetScancodeName(SDL_Scancode s) { (void)s; return ""; }
const char *psdl_GetPixelFormatName(Uint32 f) { (void)f; return ""; }
