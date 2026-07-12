#include "graphics.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "logsys.h"

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
	// Capture the finished backbuffer before presenting, if requested
	if(shot_path[0]) {
		int w, h;
		SDL_GetRendererOutputSize(renderer, &w, &h);
		SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
		if(s) {
			SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
			SDL_SaveBMP(s, shot_path);
			SDL_FreeSurface(s);
			log_msgf(INFO, "Saved screenshot %s\n", shot_path);
		}
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
	for(int i = 0; i < h; i++) {
		float t = h > 1 ? (float)i / (h - 1) : 0;
		SDL_SetRenderDrawColor(renderer,
			(int)(tr + (br - tr) * t), (int)(tg + (bg - tg) * t),
			(int)(tb + (bb - tb) * t), (int)(ta + (ba - ta) * t));
		SDL_Rect row = { x, y + i, w, 1 };
		SDL_RenderFillRect(renderer, &row);
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
			inset = radius - (int)(sqrtf((float)(rr > 0 ? rr : 0)) + 0.5f);
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
		// corner arcs
		for(int a = 0; a <= 90; a++) {
			float rad_a = a * (float)M_PI / 180.0f;
			int dx = (int)(r2 * cosf(rad_a) + 0.5f);
			int dy = (int)(r2 * sinf(rad_a) + 0.5f);
			SDL_RenderDrawPoint(renderer, xx + r2 - dx, yy + r2 - dy);                 // TL
			SDL_RenderDrawPoint(renderer, xx + ww - 1 - r2 + dx, yy + r2 - dy);        // TR
			SDL_RenderDrawPoint(renderer, xx + r2 - dx, yy + hh - 1 - r2 + dy);        // BL
			SDL_RenderDrawPoint(renderer, xx + ww - 1 - r2 + dx, yy + hh - 1 - r2 + dy); // BR
		}
	}
}

void graphics_fill_triangle(int x1, int y1, int x2, int y2, int x3, int y3, unsigned int color) {
	SDL_Color c = { color >> 24, color >> 16, color >> 8, color };
	SDL_Vertex verts[3] = {
		{ { (float)x1, (float)y1 }, c, { 0, 0 } },
		{ { (float)x2, (float)y2 }, c, { 0, 0 } },
		{ { (float)x3, (float)y3 }, c, { 0, 0 } },
	};
	SDL_RenderGeometry(renderer, NULL, verts, 3, NULL, 0);
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
