/*
 * wsdl.c — SDL2 API pieces for the Wolf4SDL riscv-stack port.
 *
 * Implements the wsdl_-renamed entry points from compat/SDL.h:
 * palettized software surfaces (Wolf pokes surface->format->palette, so
 * these are richer than sdl_lite's and live on plain malloc memory),
 * the SDL2-shaped event queue (fed from the pad via compat/lite_bridge.c),
 * and small stubs for a machine with one fixed screen and no mouse.
 *
 * Part of the Wolf4SDL riscv-stack port glue (see compat/SDL.h).
 */
#include "SDL.h"
#include "rv_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ misc ------ */

const char *wsdl_GetError(void) { return ""; }
int wsdl_SetHint(const char *n, const char *v) { (void)n; (void)v; return 0; }

/* ----------------------------------------------------------- surfaces --- */

SDL_Surface *wsdl_CreateRGBSurface(Uint32 flags, int w, int h, int bpp,
                                   Uint32 rm, Uint32 gm, Uint32 bm, Uint32 am)
{
	(void)flags; (void)rm; (void)gm; (void)bm; (void)am;
	if (bpp != 8 || w <= 0 || h <= 0)
		return NULL;                    /* this port is 8bpp-indexed only */

	SDL_Surface *s = calloc(1, sizeof(*s));
	SDL_PixelFormat *f = calloc(1, sizeof(*f));
	SDL_Palette *p = calloc(1, sizeof(*p));
	SDL_Color *colors = calloc(256, sizeof(*colors));
	void *pixels = calloc(1, (size_t)w * h);
	if (!s || !f || !p || !colors || !pixels) {
		free(s); free(f); free(p); free(colors); free(pixels);
		return NULL;
	}
	p->ncolors = 256;
	p->colors  = colors;
	f->format  = 0;                     /* "8-bit indexed" */
	f->palette = p;
	f->BitsPerPixel  = 8;
	f->BytesPerPixel = 1;
	s->pixels = pixels;
	s->w = w; s->h = h; s->pitch = w;
	s->format = f;
	return s;
}

void wsdl_FreeSurface(SDL_Surface *s)
{
	if (!s)
		return;
	free(s->format->palette->colors);
	free(s->format->palette);
	free(s->format);
	free(s->pixels);
	free(s);
}

int wsdl_SetPaletteColors(SDL_Palette *pal, const SDL_Color *colors,
                          int firstcolor, int ncolors)
{
	if (!pal || !colors || firstcolor < 0 ||
	    firstcolor + ncolors > pal->ncolors)
		return -1;
	memcpy(pal->colors + firstcolor, colors,
	       (size_t)ncolors * sizeof(*colors));
	return 0;
}

int wsdl_LockSurface(SDL_Surface *s) { (void)s; return 0; }
void wsdl_UnlockSurface(SDL_Surface *s) { (void)s; }

static void clip_rect(const SDL_Surface *s, SDL_Rect *r)
{
	if (r->x < 0) { r->w += r->x; r->x = 0; }
	if (r->y < 0) { r->h += r->y; r->y = 0; }
	if (r->x + r->w > s->w) r->w = s->w - r->x;
	if (r->y + r->h > s->h) r->h = s->h - r->y;
	if (r->w < 0) r->w = 0;
	if (r->h < 0) r->h = 0;
}

int wsdl_FillRect(SDL_Surface *dst, const SDL_Rect *rect, Uint32 color)
{
	if (!dst)
		return -1;
	SDL_Rect r = rect ? *rect : (SDL_Rect){0, 0, dst->w, dst->h};
	clip_rect(dst, &r);
	Uint8 *px = (Uint8 *)dst->pixels + (size_t)r.y * dst->pitch + r.x;
	for (int y = 0; y < r.h; y++, px += dst->pitch)
		memset(px, (int)color, (size_t)r.w);
	return 0;
}

int wsdl_BlitSurface(SDL_Surface *src, const SDL_Rect *sr,
                     SDL_Surface *dst, SDL_Rect *dr)
{
	if (!src || !dst)
		return -1;
	SDL_Rect s = sr ? *sr : (SDL_Rect){0, 0, src->w, src->h};
	clip_rect(src, &s);
	int dx = dr ? dr->x : 0, dy = dr ? dr->y : 0;
	int w = s.w, h = s.h;
	if (dx < 0) { w += dx; s.x -= dx; dx = 0; }
	if (dy < 0) { h += dy; s.y -= dy; dy = 0; }
	if (dx + w > dst->w) w = dst->w - dx;
	if (dy + h > dst->h) h = dst->h - dy;
	for (int y = 0; y < h; y++)
		memcpy((Uint8 *)dst->pixels + (size_t)(dy + y) * dst->pitch + dx,
		       (Uint8 *)src->pixels + (size_t)(s.y + y) * src->pitch + s.x,
		       (size_t)w);
	if (dr) { dr->x = dx; dr->y = dy; dr->w = w; dr->h = h; }
	return 0;
}

Uint32 wsdl_MapRGBA(const SDL_PixelFormat *f, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
	(void)f;
	return ((Uint32)a << 24) | ((Uint32)r << 16) | ((Uint32)g << 8) | b;
}

int wsdl_SaveBMP(SDL_Surface *s, const char *file)
{
	(void)s; (void)file;                /* debug screenshots: not on target */
	return -1;
}

/* ------------------------------------------------------------- video --- */

void RVSDL_InitVideo(void)
{
	rvb_video_init();
}

void RVSDL_PresentIndexed(const void *pixels, int pitch, int w, int h,
                          const void *colors256)
{
	rvb_present_indexed(pixels, pitch, w, h, colors256);
}

/* ------------------------------------------------------------ events --- */

#define EVQ_SIZE 16
static SDL_Event evq[EVQ_SIZE];
static int evq_head, evq_tail;

int wsdl_PushEvent(const SDL_Event *ev)
{
	int next = (evq_tail + 1) % EVQ_SIZE;
	if (next == evq_head)
		return -1;                      /* full */
	evq[evq_tail] = *ev;
	evq_tail = next;
	return 1;
}

int wsdl_PollEvent(SDL_Event *ev)
{
	if (evq_head != evq_tail) {
		if (ev)
			*ev = evq[evq_head];
		evq_head = (evq_head + 1) % EVQ_SIZE;
		return 1;
	}
	int sc = 0;
	int r = rvb_poll_key(&sc);
	if (r == 0)
		return 0;
	if (ev) {
		memset(ev, 0, sizeof(*ev));
		ev->type = (r == 1) ? SDL_KEYDOWN : SDL_KEYUP;
		ev->key.state = (r == 1) ? SDL_PRESSED : SDL_RELEASED;
		ev->key.keysym.scancode = (SDL_Scancode)sc;
		ev->key.keysym.mod = KMOD_NONE;
	}
	return 1;
}

/* No interrupts on this machine: "waiting for an event" = polling the pad
 * while SDL_Delay keeps the audio pumped. Returns 1 WITHOUT consuming the
 * event (SDL semantics for WaitEvent(NULL) are peek-and-block); a bounded
 * wait so a spinning caller still services its own loop periodically. */
int wsdl_WaitEvent(SDL_Event *ev)
{
	for (int i = 0; i < 40; i++) {      /* <= ~200 ms per call */
		if (evq_head != evq_tail)
			break;
		int sc = 0;
		int r = rvb_poll_key(&sc);
		if (r) {
			SDL_Event e;
			memset(&e, 0, sizeof(e));
			e.type = (r == 1) ? SDL_KEYDOWN : SDL_KEYUP;
			e.key.state = (r == 1) ? SDL_PRESSED : SDL_RELEASED;
			e.key.keysym.scancode = (SDL_Scancode)sc;
			wsdl_PushEvent(&e);
			break;
		}
		SDL_Delay(5);                   /* pumps audio */
	}
	if (ev)
		return wsdl_PollEvent(ev);
	return 1;
}

Uint8 wsdl_EventState(Uint32 type, int state)
{
	(void)type; (void)state;            /* nothing to filter: pad only */
	return 0;
}

SDL_Keymod wsdl_GetModState(void)
{
	return KMOD_NONE;                   /* pads have no modifiers */
}

/* ------------------------------------------------------------- mouse --- */

Uint32 wsdl_GetMouseState(int *x, int *y)
{
	if (x) *x = 0;
	if (y) *y = 0;
	return 0;
}

Uint32 wsdl_GetRelativeMouseState(int *x, int *y)
{
	if (x) *x = 0;
	if (y) *y = 0;
	return 0;
}

int wsdl_ShowCursor(int toggle) { (void)toggle; return 0; }
int wsdl_SetRelativeMouseMode(SDL_bool en) { (void)en; return 0; }
void wsdl_SetWindowGrab(SDL_Window *w, SDL_bool g) { (void)w; (void)g; }
void wsdl_WarpMouseInWindow(SDL_Window *w, int x, int y)
{
	(void)w; (void)x; (void)y;
}

int wsdl_ShowSimpleMessageBox(Uint32 flags, const char *title,
                              const char *message, SDL_Window *w)
{
	(void)flags; (void)w;
	printf("[%s] %s\n", title ? title : "", message ? message : "");
	return 0;
}
