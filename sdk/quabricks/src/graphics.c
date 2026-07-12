#include "graphics.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <string.h>

#include "logsys.h"

/* RVSTACK: <math.h> dropped — the console links no libm (PORTABILITY.md
 * trap #5) and sqrtf/cosf/sinf sat in per-frame draw paths where soft-float
 * would hurt anyway. Rounded corners now use the integer isqrt below and a
 * midpoint-circle arc walk; both are exact enough at 320x240 radii. */
static int isqrt32(int v) {
	if(v <= 0) return 0;
	int r = 0, bit = 1 << 14;
	while(bit > v) bit >>= 2;
	while(bit) {
		if(v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
		else r >>= 1;
		bit >>= 2;
	}
	return r;
}

SDL_Window *window;
SDL_Renderer *renderer;
long frameTime;

// Pending screenshot path (empty = none), captured during graphics_flip()
static char shot_path[256] = "";

// ---- Fonts: opened on demand per (weight, size) ----------------------------
static const char *font_files[3] = {
	"data/Lato-Regular.ttf",
	"data/Lato-Bold.ttf",
	"data/Lato-Black.ttf",
};
#define MAX_FONTS 48
static struct { int weight, size; TTF_Font *font; } fonts[MAX_FONTS];
static int font_count = 0;

static TTF_Font *get_font(int weight, int size) {
	if(weight < 0 || weight > 2) weight = FONT_REG;
	for(int i = 0; i < font_count; i++) {
		if(fonts[i].weight == weight && fonts[i].size == size) return fonts[i].font;
	}
	if(font_count >= MAX_FONTS) return fonts[0].font; // give up gracefully
	TTF_Font *f = TTF_OpenFont(font_files[weight], size);
	if(!f) {
		log_msgf(ERROR, "TTF_OpenFont(%s,%d): %s\n", font_files[weight], size, TTF_GetError());
		return font_count ? fonts[0].font : NULL;
	}
	fonts[font_count].weight = weight;
	fonts[font_count].size = size;
	fonts[font_count].font = f;
	return fonts[font_count++].font;
}

// ---- Text texture cache ----------------------------------------------------
#define MAX_TEXT 256
static struct { char key[128]; SDL_Texture *tex; int w, h; } tcache[MAX_TEXT];
static int tcount = 0;

static void wipe_text() {
	for(int i = 0; i < tcount; i++) SDL_DestroyTexture(tcache[i].tex);
	tcount = 0;
}

static int get_text(const char *str, int size, int weight, unsigned int color) {
	char key[128];
	snprintf(key, sizeof(key), "%d|%d|%08X|%s", weight, size, color, str);
	for(int i = 0; i < tcount; i++) {
		if(strcmp(tcache[i].key, key) == 0) return i;
	}
	if(tcount >= MAX_TEXT) wipe_text(); // simple eviction: start fresh
	TTF_Font *font = get_font(weight, size);
	if(!font) return -1;
	SDL_Color c = { color >> 24, color >> 16, color >> 8, color };
	SDL_Surface *surf = TTF_RenderUTF8_Blended(font, str, c);
	if(!surf) { log_msgf(ERROR, "TTF_Render: %s\n", TTF_GetError()); return -1; }
	SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
	int w = surf->w, h = surf->h;
	SDL_FreeSurface(surf);
	if(!tex) { log_msgf(ERROR, "CreateTexture: %s\n", SDL_GetError()); return -1; }
	int idx = tcount++;
	size_t klen = strlen(key);
	if(klen >= sizeof(tcache[idx].key)) klen = sizeof(tcache[idx].key) - 1;
	memcpy(tcache[idx].key, key, klen);
	tcache[idx].key[klen] = 0;
	tcache[idx].tex = tex;
	tcache[idx].w = w;
	tcache[idx].h = h;
	return idx;
}

// ---- Init / teardown -------------------------------------------------------
void graphics_init(int x, int y) {
	if(SDL_Init(SDL_INIT_VIDEO) == -1) {
		log_msgf(FATAL, "SDL_Init: %s\n", SDL_GetError());
	}
	if(SDL_CreateWindowAndRenderer(x, y, 0, &window, &renderer) == -1) {
		log_msgf(FATAL, "SDL_CreateWindowAndRenderer: %s\n", SDL_GetError());
	}
	SDL_SetWindowTitle(window, "Quabricks");
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	// Smooth scaling for text textures
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
	if(TTF_Init() == -1) {
		log_msgf(ERROR, "TTF_Init: %s\n", TTF_GetError());
	}
	frameTime = SDL_GetTicks();
}

void graphics_quit() {
	wipe_text();
	for(int i = 0; i < font_count; i++) TTF_CloseFont(fonts[i].font);
	TTF_Quit();
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
}

void graphics_request_screenshot(const char *path) {
	strncpy(shot_path, path, sizeof(shot_path) - 1);
	shot_path[sizeof(shot_path) - 1] = 0;
}

void graphics_flip() {
	long now = SDL_GetTicks();
	long wait = 1000 / 60 - (now - frameTime);
	if(wait > 0) SDL_Delay(wait);
	frameTime = SDL_GetTicks();
	/* RVSTACK: the shim has no RenderReadPixels/SaveBMP — frame capture is
	 * the PC twin's job (RVSTACK_SHOT="frame:out.bmp" env, see sdk/pc). The
	 * request is acknowledged and dropped so --shots still walks screens. */
	if(shot_path[0]) {
		log_msgf(INFO, "screenshot request ignored (use RVSTACK_SHOT)\n");
		shot_path[0] = 0;
	}
	SDL_RenderPresent(renderer);
	// Clear to the darkest background tone for the next frame
	SDL_SetRenderDrawColor(renderer, 13, 15, 28, 255);
	SDL_RenderClear(renderer);
}

// ---- Color helpers ---------------------------------------------------------
static unsigned char clampb(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

unsigned int color_scale(unsigned int c, float f) {
	int r = clampb((int)(((c >> 24) & 0xFF) * f));
	int g = clampb((int)(((c >> 16) & 0xFF) * f));
	int b = clampb((int)(((c >> 8) & 0xFF) * f));
	return (r << 24) | (g << 16) | (b << 8) | (c & 0xFF);
}

unsigned int color_lighten(unsigned int c, float amt) {
	int r = (c >> 24) & 0xFF, g = (c >> 16) & 0xFF, b = (c >> 8) & 0xFF;
	r = clampb((int)(r + (255 - r) * amt));
	g = clampb((int)(g + (255 - g) * amt));
	b = clampb((int)(b + (255 - b) * amt));
	return (r << 24) | (g << 16) | (b << 8) | (c & 0xFF);
}

unsigned int color_alpha(unsigned int c, unsigned char a) {
	return (c & 0xFFFFFF00) | a;
}

// ---- Primitives ------------------------------------------------------------
void graphics_set_color(unsigned int color) {
	SDL_SetRenderDrawColor(renderer, color >> 24, color >> 16, color >> 8, color);
}

void graphics_draw_rect(int x, int y, int w, int h) {
	SDL_Rect rect = { x, y, w, h };
	SDL_RenderFillRect(renderer, &rect);
}

void graphics_fill_rect(int x, int y, int w, int h, unsigned int color) {
	graphics_set_color(color);
	SDL_Rect rect = { x, y, w, h };
	SDL_RenderFillRect(renderer, &rect);
}

void graphics_fill_gradient(int x, int y, int w, int h, unsigned int top, unsigned int bottom) {
	int tr = (top >> 24) & 0xFF, tg = (top >> 16) & 0xFF, tb = (top >> 8) & 0xFF, ta = top & 0xFF;
	int br = (bottom >> 24) & 0xFF, bg = (bottom >> 16) & 0xFF, bb = (bottom >> 8) & 0xFF, ba = bottom & 0xFF;
	/* RVSTACK: banded (<= 16 steps) instead of per-row — the console
	 * framebuffer is 8-bit palettized and every distinct row color takes a
	 * palette slot (sdl2_lite quantizes draw colors); 240 rows of gradient
	 * would eat the whole palette for a wash the panel can't show anyway. */
	int bands = h < 16 ? (h > 0 ? h : 1) : 16;
	int y0 = 0;
	for(int i = 0; i < bands; i++) {
		int y1 = (i + 1) * h / bands;
		int t256 = bands > 1 ? i * 255 / (bands - 1) : 0;
		SDL_SetRenderDrawColor(renderer,
			tr + (br - tr) * t256 / 255, tg + (bg - tg) * t256 / 255,
			tb + (bb - tb) * t256 / 255, ta + (ba - ta) * t256 / 255);
		SDL_Rect row = { x, y + y0, w, y1 - y0 };
		SDL_RenderFillRect(renderer, &row);
		y0 = y1;
	}
}

// Filled rounded rectangle via per-row horizontal spans
void graphics_fill_round_rect(int x, int y, int w, int h, int radius, unsigned int color) {
	if(radius * 2 > w) radius = w / 2;
	if(radius * 2 > h) radius = h / 2;
	if(radius < 1) { graphics_fill_rect(x, y, w, h, color); return; }
	graphics_set_color(color);
	for(int i = 0; i < h; i++) {
		int inset = 0;
		int dtop = radius - i;               // >0 while inside top corner band
		int dbot = radius - (h - 1 - i);     // >0 while inside bottom corner band
		int d = dtop > dbot ? dtop : dbot;
		if(d > 0) {
			// horizontal inset so the corner follows a circle of given radius
			int rr = radius * radius - (radius - d) * (radius - d);
			inset = radius - isqrt32(rr > 0 ? rr : 0);   /* RVSTACK: no libm */
		}
		SDL_Rect row = { x + inset, y + i, w - inset * 2, 1 };
		if(row.w > 0) SDL_RenderFillRect(renderer, &row);
	}
}

void graphics_round_rect_outline(int x, int y, int w, int h, int radius, int thickness, unsigned int color) {
	// Draw as an outer rounded rect with an inner rounded rect punched out
	// using the current draw color; since we can't "erase", approximate the
	// border by drawing `thickness` concentric rounded outlines 1px apart.
	for(int t = 0; t < thickness; t++) {
		if(radius * 2 > w || radius * 2 > h) break;
		graphics_set_color(color);
		int rad = radius;
		int xx = x + t, yy = y + t, ww = w - 2 * t, hh = h - 2 * t;
		if(ww <= 0 || hh <= 0) break;
		// top & bottom edges
		int r2 = rad - t; if(r2 < 0) r2 = 0;
		SDL_Rect topr = { xx + r2, yy, ww - 2 * r2, 1 };
		SDL_Rect botr = { xx + r2, yy + hh - 1, ww - 2 * r2, 1 };
		SDL_RenderFillRect(renderer, &topr);
		SDL_RenderFillRect(renderer, &botr);
		// left & right edges
		SDL_Rect lr = { xx, yy + r2, 1, hh - 2 * r2 };
		SDL_Rect rr = { xx + ww - 1, yy + r2, 1, hh - 2 * r2 };
		SDL_RenderFillRect(renderer, &lr);
		SDL_RenderFillRect(renderer, &rr);
		/* RVSTACK: corner arcs via integer midpoint circle instead of a
		 * 91-step cosf/sinf walk (no libm; trig per outline per frame was
		 * soft-float suicide on the 66 MHz core) */
		int adx = r2, ady = 0, err = 1 - r2;
		while(adx >= ady) {
			int cxl = xx + r2, cxr = xx + ww - 1 - r2;
			int cyt = yy + r2, cyb = yy + hh - 1 - r2;
			SDL_RenderDrawPoint(renderer, cxl - adx, cyt - ady);   // TL
			SDL_RenderDrawPoint(renderer, cxl - ady, cyt - adx);
			SDL_RenderDrawPoint(renderer, cxr + adx, cyt - ady);   // TR
			SDL_RenderDrawPoint(renderer, cxr + ady, cyt - adx);
			SDL_RenderDrawPoint(renderer, cxl - adx, cyb + ady);   // BL
			SDL_RenderDrawPoint(renderer, cxl - ady, cyb + adx);
			SDL_RenderDrawPoint(renderer, cxr + adx, cyb + ady);   // BR
			SDL_RenderDrawPoint(renderer, cxr + ady, cyb + adx);
			ady++;
			if(err < 0) err += 2 * ady + 1;
			else { adx--; err += 2 * (ady - adx) + 1; }
		}
	}
}

void graphics_fill_triangle(int x1, int y1, int x2, int y2, int x3, int y3, unsigned int color) {
	/* RVSTACK: SDL_RenderGeometry replaced with an integer scanline fill
	 * (16.16 edge stepping) — the shim has no vertex renderer, and the only
	 * triangles here are the little level-select arrows */
	graphics_set_color(color);
	// sort by y so (x1,y1) is top and (x3,y3) is bottom
	int tx, ty;
	if(y1 > y2) { tx = x1; x1 = x2; x2 = tx; ty = y1; y1 = y2; y2 = ty; }
	if(y1 > y3) { tx = x1; x1 = x3; x3 = tx; ty = y1; y1 = y3; y3 = ty; }
	if(y2 > y3) { tx = x2; x2 = x3; x3 = tx; ty = y2; y2 = y3; y3 = ty; }
	if(y3 == y1) return;
	for(int y = y1; y <= y3; y++) {
		// x on the long edge (1->3), and on the split edge (1->2 or 2->3)
		int xa = x1 + (int)((long)(x3 - x1) * (y - y1) / (y3 - y1));
		int xb;
		if(y < y2 && y2 != y1)
			xb = x1 + (int)((long)(x2 - x1) * (y - y1) / (y2 - y1));
		else if(y3 != y2)
			xb = x2 + (int)((long)(x3 - x2) * (y - y2) / (y3 - y2));
		else
			xb = x2;
		if(xa > xb) { tx = xa; xa = xb; xb = tx; }
		SDL_Rect row = { xa, y, xb - xa + 1, 1 };
		SDL_RenderFillRect(renderer, &row);
	}
}

// ---- Game blocks -----------------------------------------------------------
void graphics_draw_block(int x, int y, int size, unsigned int base, unsigned char alpha) {
	int radius = size / 6;
	if(radius < 2) radius = 2;
	unsigned int grout  = color_alpha(color_scale(base, 0.45f), alpha);
	unsigned int face   = color_alpha(base, alpha);
	unsigned int hi     = color_alpha(color_lighten(base, 0.6f), alpha);
	unsigned int gloss  = color_alpha(0xFFFFFF00, (unsigned char)(alpha * 0.22f));

	// Full-cell dark base so seams between adjacent blocks read as thin grout,
	// never the background showing through the rounded corners
	graphics_fill_rect(x, y, size, size, grout);

	// Raised, rounded face inset from the grout
	int inset = size / 14; if(inset < 1) inset = 1;
	int fx = x + inset, fy = y + inset, fw = size - inset * 2, fh = size - inset * 2;
	graphics_fill_round_rect(fx, fy, fw, fh, radius, face);

	// Glossy sheen over the upper portion
	graphics_fill_round_rect(fx + 1, fy + 1, fw - 2, fh / 2 - 1, radius, gloss);

	// Bright top + left inner lip for a crisp 3D edge
	graphics_set_color(hi);
	SDL_Rect topEdge  = { fx + radius, fy, fw - radius * 2, 2 };
	SDL_Rect leftEdge = { fx, fy + radius, 2, fh - radius * 2 };
	SDL_RenderFillRect(renderer, &topEdge);
	SDL_RenderFillRect(renderer, &leftEdge);
}

void graphics_draw_ghost(int x, int y, int size, unsigned int base) {
	int radius = size / 5;
	if(radius < 2) radius = 2;
	// faint translucent fill
	graphics_fill_round_rect(x + 1, y + 1, size - 2, size - 2, radius,
		color_alpha(base, 26));
	// colored outline
	graphics_round_rect_outline(x + 1, y + 1, size - 2, size - 2, radius, 2,
		color_alpha(color_lighten(base, 0.1f), 110));
}

// ---- Text ------------------------------------------------------------------
void graphics_text(const char *str, int x, int y, int size, int weight,
		unsigned int color, int align) {
	if(!str || str[0] == '\0') return;
	/* RVSTACK: the shim's text textures are opaque-where-inked (indexed, no
	 * per-texture alpha), so translucent text colors are pre-blended toward
	 * the dark backdrop tone here — pulses/dimming keep working. */
	unsigned int a = color & 0xFF;
	if(a < 255) {
		unsigned int r = ((color >> 24) & 0xFF) * a / 255 + 13 * (255 - a) / 255;
		unsigned int g = ((color >> 16) & 0xFF) * a / 255 + 15 * (255 - a) / 255;
		unsigned int b = ((color >> 8) & 0xFF) * a / 255 + 28 * (255 - a) / 255;
		color = (r << 24) | (g << 16) | (b << 8) | 0xFF;
	}
	int idx = get_text(str, size, weight, color);
	if(idx < 0) return;
	int drawx = x;
	if(align == ALIGN_CENTER) drawx = x - tcache[idx].w / 2;
	else if(align == ALIGN_RIGHT) drawx = x - tcache[idx].w;
	SDL_Rect d = { drawx, y, tcache[idx].w, tcache[idx].h };
	SDL_RenderCopy(renderer, tcache[idx].tex, NULL, &d);
}

int graphics_text_width(const char *str, int size, int weight) {
	if(!str || str[0] == '\0') return 0;
	int idx = get_text(str, size, weight, COLOR_WHITE);
	return idx < 0 ? 0 : tcache[idx].w;
}

int graphics_text_height(int size, int weight) {
	TTF_Font *f = get_font(weight, size);
	return f ? TTF_FontHeight(f) : size;
}
