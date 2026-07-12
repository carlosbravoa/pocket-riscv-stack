/* 
 * OpenTyrian: A modern cross-platform port of Tyrian
 * Copyright (C) 2007-2009  The OpenTyrian Development Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include "backgrnd.h"

#include "config.h"
#include "mtrand.h"
#include "opentyr.h"
#include "varz.h"
#include "video.h"

#include "hal.h"
#ifndef RVSTACK_PC
#include <system.h>
#else
#define flush_cpu_dcache_range(p, n) ((void)0)
#endif
#include <assert.h>
#include <stdint.h>
#include <string.h>

/*Special Background 2 and Background 3*/

/*Back Pos 3*/
JE_word backPos, backPos2, backPos3;
JE_word backMove, backMove2, backMove3;

/*Main Maps*/
JE_word mapX, mapY, mapX2, mapX3, mapY2, mapY3;
JE_byte **mapYPos, **mapY2Pos, **mapY3Pos;
JE_word mapXPos, oldMapXOfs, mapXOfs, mapX2Ofs, mapX2Pos, mapX3Pos, oldMapX3Ofs, mapX3Ofs, tempMapXOfs;
intptr_t mapXbpPos, mapX2bpPos, mapX3bpPos;
JE_byte map1YDelay, map1YDelayMax, map2YDelay, map2YDelayMax;

JE_boolean  anySmoothies;
JE_byte     smoothie_data[9]; /* [1..9] */

void JE_darkenBackground(JE_word neat)  /* wild detail level */
{
	/* RVSTACK: the original per-pixel expression ran twice per frame under
	 * superWild — 55% of ALL CPU time (RTL commit-PC profile, v0.19.4).
	 * For a given pixel byte and 4-bit noise term the output is fixed, so
	 * precompute a 4 KB LUT; the loop-carried terms (s[-2], the row above,
	 * the (x-neat-y)>>2 ramp) are computed exactly as before. Verified
	 * bit-identical to the original over randomized + iterated buffers. */
	static Uint8 lut[256 * 16];
	static bool lut_ok = false;
	if (!lut_ok)
	{
		for (int p = 0; p < 256; p++)
			for (int n = 0; n < 16; n++)
				lut[(p << 4) | n] =
				    (Uint8)((((((p & 0x0f) << 4) - (p & 0x0f)) + n) >> 4) | (p & 0xf0));
		lut_ok = true;
	}

	Uint8 *s = VGAScreen->pixels; /* screen pointer, 8-bit specific */
	int x, y;

	s += 24;

	for (y = 184; y; y--)
	{
		const Uint8 *up = (y == 184) ? NULL : s - (VGAScreen->pitch - 1);
		int t = 264 - (int)neat - y;        /* == x - neat - y at x = 264 */
		if (up)
		{
			for (x = 264; x; x--, t--)
			{
				unsigned n = ((t >> 2) + s[-2] + *up++) & 0x0f;
				*s = lut[((unsigned)*s << 4) | n];
				s++;
			}
		}
		else
		{
			for (x = 264; x; x--, t--)
			{
				unsigned n = ((t >> 2) + s[-2]) & 0x0f;
				*s = lut[((unsigned)*s << 4) | n];
				s++;
			}
		}
		s += VGAScreen->pitch - 264;
	}
}

void blit_background_row(SDL_Surface *surface, int x, int y, Uint8 **map)
{
	assert(surface->format->BitsPerPixel == 8);
	
	Uint8 *pixels = (Uint8 *)surface->pixels + (y * surface->pitch) + x,
	      *pixels_ll = (Uint8 *)surface->pixels,  // lower limit
	      *pixels_ul = (Uint8 *)surface->pixels + (surface->h * surface->pitch);  // upper limit

	/* RVSTACK: colorkey DMA path (v0.20.0 cores, HAL_FEAT_BLITKEY). A fully
	 * visible row-call is 12 independent 24x28 tile blits; the engine skips
	 * transparent beats in fabric, so this replaces the hottest CPU loop in
	 * the game (46% of all CPU at the v0.19.7 profile). Coherence: write
	 * back + invalidate our span first so stale CPU lines neither mask nor
	 * clobber the DMA's work; sprites composited afterwards refetch fresh.
	 * Clipped rows and pre-v0.20 cores fall through to the CPU loops. */
	if ((sys_caps()->features & HAL_FEAT_BLITKEY)
	    && pixels >= pixels_ll
	    && pixels + 27 * surface->pitch + 12 * 24 <= pixels_ul)
	{
		flush_cpu_dcache_range(pixels, 27 * surface->pitch + 12 * 24);
		for (int tile = 0; tile < 12; tile++)
		{
			if (map[tile] == NULL)
				continue;
			if (blit_ck(pixels + tile * 24, map[tile], 24, 28,
			            24, surface->pitch) != 0)
				goto cpu_path;          /* engine refused: draw it all in C */
			blit_wait();
		}
		return;
	}
cpu_path:

	for (int y = 0; y < 28; y++)
	{
		// not drawing on screen yet; skip y
		if ((pixels + (12 * 24)) < pixels_ll)
		{
			pixels += surface->pitch;
			continue;
		}

		/* RVSTACK: each row-call walks one contiguous 288-byte span, so the
		 * bounds question is answerable once per row. Fully-visible rows
		 * (the overwhelming majority) run without the two per-pixel bounds
		 * checks; clipped rows keep the original careful loop. */
		if (pixels >= pixels_ll && pixels + 12 * 24 <= pixels_ul)
		{
			for (int tile = 0; tile < 12; tile++)
			{
				const Uint8 *data = *(map + tile);
				if (data == NULL)
				{
					pixels += 24;
					continue;
				}
				data += y * 24;
				/* Terrain tiles are mostly opaque: when src and dst are
				 * co-aligned, run 4 px per word — store whole words with
				 * no zero byte (the (w-0x01010101)&~w&0x80808080 trick),
				 * fall back per-byte only inside mixed words. 46% of all
				 * CPU lived in this loop at the v0.19.7 RTL profile. */
				if ((((uintptr_t)pixels ^ (uintptr_t)data) & 3) == 0)
				{
					int i = 0;
					while (((uintptr_t)(pixels + i) & 3) && i < 24)
					{
						if (data[i] != 0)
							pixels[i] = data[i];
						i++;
					}
					for (; i + 4 <= 24; i += 4)
					{
						uint32_t w;
						memcpy(&w, data + i, 4);
						if ((w - 0x01010101u) & ~w & 0x80808080u)
						{
							if (data[i]     != 0) pixels[i]     = data[i];
							if (data[i + 1] != 0) pixels[i + 1] = data[i + 1];
							if (data[i + 2] != 0) pixels[i + 2] = data[i + 2];
							if (data[i + 3] != 0) pixels[i + 3] = data[i + 3];
						}
						else
							memcpy(pixels + i, &w, 4);
					}
					for (; i < 24; i++)
						if (data[i] != 0)
							pixels[i] = data[i];
				}
				else
				{
					for (int i = 0; i < 24; i++)
						if (data[i] != 0)
							pixels[i] = data[i];
				}
				pixels += 24;
			}
		}
		else
		for (int tile = 0; tile < 12; tile++)
		{
			Uint8 *data = *(map + tile);

			// no tile; skip tile
			if (data == NULL)
			{
				pixels += 24;
				continue;
			}

			data += y * 24;

			for (int x = 24; x; x--)
			{
				if (pixels >= pixels_ul)
					return;
				if (pixels >= pixels_ll && *data != 0)
					*pixels = *data;

				pixels++;
				data++;
			}
		}

		pixels += surface->pitch - 12 * 24;
	}
}

void blit_background_row_blend(SDL_Surface *surface, int x, int y, Uint8 **map)
{
	assert(surface->format->BitsPerPixel == 8);
	
	Uint8 *pixels = (Uint8 *)surface->pixels + (y * surface->pitch) + x,
	      *pixels_ll = (Uint8 *)surface->pixels,  // lower limit
	      *pixels_ul = (Uint8 *)surface->pixels + (surface->h * surface->pitch);  // upper limit
	
	/* RVSTACK: same row-level bounds classification as blit_background_row,
	 * plus the blend collapses to a 4 KB LUT indexed by (tile byte, low
	 * nibble of the screen pixel) — same trick as JE_darkenBackground. */
	static Uint8 blend_lut[256 * 16];
	static bool blend_lut_ok = false;
	if (!blend_lut_ok)
	{
		for (int d = 0; d < 256; d++)
			for (int p = 0; p < 16; p++)
				blend_lut[(d << 4) | p] =
				    (Uint8)((d & 0xf0) | (((p + (d & 0x0f)) / 2)));
		blend_lut_ok = true;
	}

	for (int y = 0; y < 28; y++)
	{
		// not drawing on screen yet; skip y
		if ((pixels + (12 * 24)) < pixels_ll)
		{
			pixels += surface->pitch;
			continue;
		}

		if (pixels >= pixels_ll && pixels + 12 * 24 <= pixels_ul)
		{
			for (int tile = 0; tile < 12; tile++)
			{
				const Uint8 *data = *(map + tile);
				if (data == NULL)
				{
					pixels += 24;
					continue;
				}
				data += y * 24;
				for (int i = 0; i < 24; i++)
					if (data[i] != 0)
						pixels[i] = blend_lut[((unsigned)data[i] << 4) |
						                      (pixels[i] & 0x0f)];
				pixels += 24;
			}
		}
		else
		for (int tile = 0; tile < 12; tile++)
		{
			Uint8 *data = *(map + tile);

			// no tile; skip tile
			if (data == NULL)
			{
				pixels += 24;
				continue;
			}

			data += y * 24;

			for (int x = 24; x; x--)
			{
				if (pixels >= pixels_ul)
					return;
				if (pixels >= pixels_ll && *data != 0)
					*pixels = (*data & 0xf0) | (((*pixels & 0x0f) + (*data & 0x0f)) / 2);

				pixels++;
				data++;
			}
		}

		pixels += surface->pitch - 12 * 24;
	}
}

void draw_background_1(SDL_Surface *surface)
{
	SDL_FillRect(surface, NULL, 0);
	
	Uint8 **map = (Uint8 **)mapYPos + mapXbpPos - 12;
	
	for (int i = -1; i < 7; i++)
	{
		blit_background_row(surface, mapXPos, (i * 28) + backPos, map);
		
		map += 14;
	}
}

void draw_background_2(SDL_Surface *surface)
{
	if (map2YDelayMax > 1 && backMove2 < 2)
		backMove2 = (map2YDelay == 1) ? 1 : 0;
	
	if (background2 != 0)
	{
		// water effect combines background 1 and 2 by synchronizing the x coordinate
		int x = smoothies[1] ? mapXPos : mapX2Pos;
		
		Uint8 **map = (Uint8 **)mapY2Pos + (smoothies[1] ? mapXbpPos : mapX2bpPos) - 12;
		
		for (int i = -1; i < 7; i++)
		{
			blit_background_row(surface, x, (i * 28) + backPos2, map);
			
			map += 14;
		}
	}
	
	/*Set Movement of background*/
	if (--map2YDelay == 0)
	{
		map2YDelay = map2YDelayMax;
		
		backPos2 += backMove2;
		
		if (backPos2 >  27)
		{
			backPos2 -= 28;
			mapY2--;
			mapY2Pos -= 14;  /*Map Width*/
		}
	}
}

void draw_background_2_blend(SDL_Surface *surface)
{
	if (map2YDelayMax > 1 && backMove2 < 2)
		backMove2 = (map2YDelay == 1) ? 1 : 0;
	
	Uint8 **map = (Uint8 **)mapY2Pos + mapX2bpPos - 12;
	
	for (int i = -1; i < 7; i++)
	{
		blit_background_row_blend(surface, mapX2Pos, (i * 28) + backPos2, map);
		
		map += 14;
	}
	
	/*Set Movement of background*/
	if (--map2YDelay == 0)
	{
		map2YDelay = map2YDelayMax;
		
		backPos2 += backMove2;
		
		if (backPos2 >  27)
		{
			backPos2 -= 28;
			mapY2--;
			mapY2Pos -= 14;  /*Map Width*/
		}
	}
}

void draw_background_3(SDL_Surface *surface)
{
	/* Movement of background */
	backPos3 += backMove3;
	
	if (backPos3 > 27)
	{
		backPos3 -= 28;
		mapY3--;
		mapY3Pos -= 15;   /*Map Width*/
	}
	
	Uint8 **map = (Uint8 **)mapY3Pos + mapX3bpPos - 12;
	
	for (int i = -1; i < 7; i++)
	{
		blit_background_row(surface, mapX3Pos, (i * 28) + backPos3, map);
		
		map += 15;
	}
}

void JE_filterScreen(JE_shortint col, JE_shortint int_)
{
	Uint8 *s = NULL; /* screen pointer, 8-bit specific */
	int x, y;
	unsigned int temp;
	
	if (filterFade)
	{
		levelBrightness += levelBrightnessChg;
		if ((filterFadeStart && levelBrightness < -14) || levelBrightness > 14)
		{
			levelBrightnessChg = -levelBrightnessChg;
			filterFadeStart = false;
			levelFilter = levelFilterNew;
		}
		if (!filterFadeStart && levelBrightness == 0)
		{
			filterFade = false;
			levelBrightness = -99;
		}
	}
	
	if (col != -99 && filtrationAvail)
	{
		s = VGAScreen->pixels;
		s += 24;
		
		col <<= 4;
		
		for (y = 184; y; y--)
		{
			for (x = 264; x; x--)
			{
				*s = col | (*s & 0x0f);
				s++;
			}
			s += VGAScreen->pitch - 264;
		}
	}
	
	if (int_ != -99 && explosionTransparent)
	{
		s = VGAScreen->pixels;
		s += 24;
		
		for (y = 184; y; y--)
		{
			for (x = 264; x; x--)
			{
				temp = (*s & 0x0f) + int_;
				*s = (*s & 0xf0) | (temp >= 0x1f ? 0 : (temp >= 0x0f ? 0x0f : temp));
				s++;
			}
			s += VGAScreen->pitch - 264;
		}
	}
}

void JE_checkSmoothies(void)
{
	anySmoothies = (processorType > 2 && (smoothies[1-1] || smoothies[2-1])) || (processorType > 1 && (smoothies[3-1] || smoothies[4-1] || smoothies[5-1]));
}

void lava_filter(SDL_Surface *dst, SDL_Surface *src)
{
	assert(src->format->BitsPerPixel == 8 && dst->format->BitsPerPixel == 8);
	
	/* we don't need to check for over-reading the pixel surfaces since we only
	 * read from the top 185+1 scanlines, and there should be 320 */
	
	const int dst_pitch = dst->pitch;
	Uint8 *dst_pixel = (Uint8 *)dst->pixels + (185 * dst_pitch);
	const Uint8 * const dst_pixel_ll = (Uint8 *)dst->pixels;  // lower limit
	
	const int src_pitch = src->pitch;
	const Uint8 *src_pixel = (Uint8 *)src->pixels + (185 * src->pitch);
	const Uint8 * const src_pixel_ll = (Uint8 *)src->pixels;  // lower limit
	
	int w = 320 * 185 - 1;
	
	for (int y = 185 - 1; y >= 0; --y)
	{
		dst_pixel -= (dst_pitch - 320);  // in case pitch is not 320
		src_pixel -= (src_pitch - 320);  // in case pitch is not 320
		
		for (int x = 320 - 1; x >= 0; x -= 8)
		{
			int waver = abs(((w >> 9) & 0x0f) - 8) - 1;
			w -= 8;
			
			for (int xi = 8 - 1; xi >= 0; --xi)
			{
				--dst_pixel;
				--src_pixel;
				
				// value is average value of source pixel (2x), destination pixel above, and destination pixel below (all with waver)
				// hue is red
				Uint8 value = 0;
				
				if (src_pixel + waver >= src_pixel_ll)
					value += (*(src_pixel + waver) & 0x0f) * 2;
				value += *(dst_pixel + waver + dst_pitch) & 0x0f;
				if (dst_pixel + waver - dst_pitch >= dst_pixel_ll)
					value += *(dst_pixel + waver - dst_pitch) & 0x0f;
				
				*dst_pixel = (value / 4) | 0x70;
			}
		}
	}
}

void water_filter(SDL_Surface *dst, SDL_Surface *src)
{
	assert(src->format->BitsPerPixel == 8 && dst->format->BitsPerPixel == 8);
	
	Uint8 hue = smoothie_data[1] << 4;
	
	/* we don't need to check for over-reading the pixel surfaces since we only
	 * read from the top 185+1 scanlines, and there should be 320 */
	
	const int dst_pitch = dst->pitch;
	Uint8 *dst_pixel = (Uint8 *)dst->pixels + (185 * dst_pitch);
	
	const Uint8 *src_pixel = (Uint8 *)src->pixels + (185 * src->pitch);
	
	int w = 320 * 185 - 1;
	
	for (int y = 185 - 1; y >= 0; --y)
	{
		dst_pixel -= (dst_pitch - 320);  // in case pitch is not 320
		src_pixel -= (src->pitch - 320);  // in case pitch is not 320
		
		for (int x = 320 - 1; x >= 0; x -= 8)
		{
			int waver = abs(((w >> 10) & 0x07) - 4) - 1;
			w -= 8;
			
			for (int xi = 8 - 1; xi >= 0; --xi)
			{
				--dst_pixel;
				--src_pixel;
				
				// pixel is copied from source if not blue
				// otherwise, value is average of value of source pixel and destination pixel below (with waver)
				if ((*src_pixel & 0x30) == 0)
				{
					*dst_pixel = *src_pixel;
				}
				else
				{
					Uint8 value = *src_pixel & 0x0f;
					value += *(dst_pixel + waver + dst_pitch) & 0x0f;
					*dst_pixel = (value / 2) | hue;
				}
			}
		}
	}
}

void iced_blur_filter(SDL_Surface *dst, SDL_Surface *src)
{
	assert(src->format->BitsPerPixel == 8 && dst->format->BitsPerPixel == 8);
	
	Uint8 *dst_pixel = dst->pixels;
	const Uint8 *src_pixel = src->pixels;
	
	for (int y = 0; y < 184; ++y)
	{
		for (int x = 0; x < 320; ++x)
		{
			// value is average value of source pixel and destination pixel
			// hue is icy blue
			
			const Uint8 value = (*src_pixel & 0x0f) + (*dst_pixel & 0x0f);
			*dst_pixel = (value / 2) | 0x80;
			
			++dst_pixel;
			++src_pixel;
		}
		
		dst_pixel += (dst->pitch - 320);  // in case pitch is not 320
		src_pixel += (src->pitch - 320);  // in case pitch is not 320
	}
}

void blur_filter(SDL_Surface *dst, SDL_Surface *src)
{
	assert(src->format->BitsPerPixel == 8 && dst->format->BitsPerPixel == 8);
	
	Uint8 *dst_pixel = dst->pixels;
	const Uint8 *src_pixel = src->pixels;
	
	for (int y = 0; y < 184; ++y)
	{
		for (int x = 0; x < 320; ++x)
		{
			// value is average value of source pixel and destination pixel
			// hue is source pixel hue
			
			const Uint8 value = (*src_pixel & 0x0f) + (*dst_pixel & 0x0f);
			*dst_pixel = (value / 2) | (*src_pixel & 0xf0);
			
			++dst_pixel;
			++src_pixel;
		}
		
		dst_pixel += (dst->pitch - 320);  // in case pitch is not 320
		src_pixel += (src->pitch - 320);  // in case pitch is not 320
	}
}

/* Background Starfield */
typedef struct
{
	Uint8 color;
	JE_word position; // relies on overflow wrap-around
	int speed;
} StarfieldStar;

#define MAX_STARS 100
#define STARFIELD_HUE 0x90
static StarfieldStar starfield_stars[MAX_STARS];
int starfield_speed;

void initialize_starfield(void)
{
	for (int i = MAX_STARS-1; i >= 0; --i)
	{
		starfield_stars[i].position = mt_rand() % 320 + mt_rand() % 200 * VGAScreen->pitch;
		starfield_stars[i].speed = mt_rand() % 3 + 2;
		starfield_stars[i].color = mt_rand() % 16 + STARFIELD_HUE;
	}
}

void update_and_draw_starfield(SDL_Surface* surface, int move_speed)
{
	Uint8* p = (Uint8*)surface->pixels;

	for (int i = MAX_STARS-1; i >= 0; --i)
	{
		StarfieldStar* star = &starfield_stars[i];

		star->position += (star->speed + move_speed) * surface->pitch;

		if (star->position < 177 * surface->pitch)
		{
			if (p[star->position] == 0)
			{
				p[star->position] = star->color;
			}

			// If star is bright enough, draw surrounding pixels
			if (star->color - 4 >= STARFIELD_HUE)
			{
				if (p[star->position + 1] == 0)
					p[star->position + 1] = star->color - 4;

				if (star->position > 0 && p[star->position - 1] == 0)
					p[star->position - 1] = star->color - 4;

				if (p[star->position + surface->pitch] == 0)
					p[star->position + surface->pitch] = star->color - 4;

				if (star->position >= surface->pitch && p[star->position - surface->pitch] == 0)
					p[star->position - surface->pitch] = star->color - 4;
			}
		}
	}
}
