/*
 * pop_png.c — PNG sprite loading for the SDLPoP port. The game's loose data
 * (data/<SET>/resNNN.png) is INDEXED PNG (1/4/8-bit palette); the engine
 * recolors sprite sets per level via set_chtab_palette, so the palette MUST be
 * preserved. We decode to an 8bpp SDL_Surface + its palette (lodepng, integer,
 * works on rv32). Replaces SDL_image's IMG_Load_RW + a minimal memory RWops.
 *
 * SPDX-License-Identifier: BSD-2-Clause (shim); lodepng is zlib/public-domain.
 */
#include "SDL.h"                 /* our shadow header (psdl_ surface API) */
#include "lodepng.h"

#include <stdlib.h>
#include <string.h>

struct SDL_RWops { const unsigned char *mem; int size; int pos; };

SDL_RWops *psdl_RWFromConstMem(const void *mem, int size)
{
	SDL_RWops *rw = (SDL_RWops *)calloc(1, sizeof *rw);
	if (rw) { rw->mem = (const unsigned char *)mem; rw->size = size; rw->pos = 0; }
	return rw;
}
size_t psdl_RWread(SDL_RWops *rw, void *ptr, size_t size, size_t maxnum)
{
	if (!rw || !size) return 0;
	size_t avail = (size_t)(rw->size - rw->pos) / size;
	if (maxnum > avail) maxnum = avail;
	memcpy(ptr, rw->mem + rw->pos, size * maxnum);
	rw->pos += (int)(size * maxnum);
	return maxnum;
}
long psdl_RWtell(SDL_RWops *rw) { return rw ? rw->pos : -1; }
int  psdl_RWclose(SDL_RWops *rw) { free(rw); return 0; }

/* Raw pre-decoded blob (tools/png2raw.py): "RVI1" w h ncolors pal[] idx[].
 * The console reads this instead of decoding PNG at runtime (lodepng is ~1.17M
 * cycles/glyph on rv32). Produces the same 8bpp+palette surface as the PNG
 * path; the caller (load_image) still colorkeys index 0. */
static SDL_Surface *load_rvi1(const unsigned char *in, size_t insize)
{
	if (insize < 10) return NULL;
	unsigned w  = in[4] | (in[5] << 8);
	unsigned h  = in[6] | (in[7] << 8);
	unsigned nc = in[8] | (in[9] << 8);
	const unsigned char *pal = in + 10;
	const unsigned char *px  = pal + (size_t)nc * 3;
	if (10 + (size_t)nc * 3 + (size_t)w * h > insize) return NULL;
	SDL_Surface *img = SDL_CreateRGBSurface(0, (int)w, (int)h, 8, 0, 0, 0, 0);
	if (!img) return NULL;
	for (unsigned y = 0; y < h; y++)          /* pitch == w for our 8bpp */
		memcpy((unsigned char *)img->pixels + (size_t)y * img->pitch,
		       px + (size_t)y * w, w);
	SDL_Palette *p = img->format->palette;
	for (unsigned i = 0; i < nc && i < 256; i++) {
		p->colors[i].r = pal[i*3+0];
		p->colors[i].g = pal[i*3+1];
		p->colors[i].b = pal[i*3+2];
		p->colors[i].a = 255;
	}
	return img;
}

SDL_Surface *psdl_IMG_Load_RW(SDL_RWops *rw, int freesrc)
{
	(void)freesrc;
	if (!rw) return NULL;
	const unsigned char *in = rw->mem;
	size_t insize = (size_t)rw->size;

	if (insize >= 4 && in[0]=='R' && in[1]=='V' && in[2]=='I' && in[3]=='1')
		return load_rvi1(in, insize);      /* pre-decoded fast path */

	LodePNGState st;
	lodepng_state_init(&st);
	st.decoder.color_convert = 0;            /* keep the PNG's native format */
	unsigned char *out = NULL; unsigned w = 0, h = 0;
	unsigned err = lodepng_decode(&out, &w, &h, &st, in, insize);
	SDL_Surface *img = NULL;

	if (!err && st.info_png.color.colortype == LCT_PALETTE) {
		int bd = st.info_png.color.bitdepth;         /* 1, 2, 4, or 8 */
		img = SDL_CreateRGBSurface(0, (int)w, (int)h, 8, 0, 0, 0, 0);
		unsigned char *dst = (unsigned char *)img->pixels;
		size_t rowbytes = ((size_t)w * bd + 7) / 8;
		for (unsigned y = 0; y < h; y++) {
			const unsigned char *row = out + (size_t)y * rowbytes;
			for (unsigned x = 0; x < w; x++) {
				unsigned idx;
				if (bd == 8) {
					idx = row[x];
				} else {                     /* MSB-first sub-byte packing */
					unsigned ppb = 8u / bd, mask = (1u << bd) - 1u;
					unsigned shift = 8u - bd - (x % ppb) * bd;
					idx = (row[x / ppb] >> shift) & mask;
				}
				dst[(size_t)y * img->pitch + x] = (unsigned char)idx;
			}
		}
		SDL_Palette *pal = img->format->palette;
		unsigned n = (unsigned)st.info_png.color.palettesize;
		for (unsigned i = 0; i < n && i < 256; i++) {
			pal->colors[i].r = st.info_png.color.palette[i*4+0];
			pal->colors[i].g = st.info_png.color.palette[i*4+1];
			pal->colors[i].b = st.info_png.color.palette[i*4+2];
			pal->colors[i].a = st.info_png.color.palette[i*4+3];
		}
	} else if (!err) {
		/* non-palette PNG (rare here): decode RGBA -> 32bpp truecolor */
		free(out); out = NULL;
		err = lodepng_decode_memory(&out, &w, &h, in, insize, LCT_RGBA, 8);
		if (!err) {
			img = SDL_CreateRGBSurface(0, (int)w, (int)h, 32, 0, 0, 0, 0);
			memcpy(img->pixels, out, (size_t)w * h * 4);  /* R,G,B,A order */
		}
	}
	free(out);
	lodepng_state_cleanup(&st);
	return img;
}
