/*
 * id_vl_rvstack.c — Omnispeak VL_Backend on the riscv-stack HAL.
 *
 * Structure follows src/id_vl_null.c (whose software-PAL8 surface ops are
 * exactly what we need) with a real present(): the 320x200 EGA window is
 * copied out of the (336x224, scroll-panned) buffer surface into the HAL's
 * 320x240 indexed backbuffer, letterboxed by 20 rows of EGA border color —
 * 320x200 is baked into Keen (mode 0xD), so letterboxing is the authentic
 * choice; the 16-color EGA palette rides in via palette_set.
 *
 * Timing also lives here (this platform has no timer thread): present() /
 * waitVBLs() pump the 140/560 Hz sound+tick service via RVK_TimerPump.
 *
 * Part of the Omnispeak riscv-stack port glue. SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hal.h" /* FIRST (trap #2) */
#include "rv_keen.h"

#include <stdlib.h>
#include <string.h>

#include "id_sd.h"
#include "id_us.h"
#include "id_vl.h"
#include "id_vl_private.h"

#include "ck_cross.h"

static int vl_rv_screenWidth;
static int vl_rv_screenHeight;

typedef struct VL_RV_Surface
{
	VL_SurfaceUsage use;
	int w, h;
	uint8_t *data;
} VL_RV_Surface;

/* ------------------------------------------------------- surface ops -- */

static void VL_RV_SetVideoMode(int mode)
{
	if (mode == 0xD)
	{
		vl_rv_screenWidth = VL_EGAVGA_GFX_WIDTH;
		vl_rv_screenHeight = VL_EGAVGA_GFX_HEIGHT;
		RVK_Beacon(2);
	}
}

static void *VL_RV_CreateSurface(int w, int h, VL_SurfaceUsage usage)
{
	VL_RV_Surface *surf = (VL_RV_Surface *)malloc(sizeof(VL_RV_Surface));
	surf->use = usage;
	surf->w = w;
	surf->h = h;
	surf->data = (uint8_t *)calloc(1, w * h);
	return surf;
}

static void VL_RV_DestroySurface(void *surface)
{
	VL_RV_Surface *surf = (VL_RV_Surface *)surface;
	if (surf->data)
		free(surf->data);
	free(surf);
}

static long VL_RV_GetSurfaceMemUse(void *surface)
{
	VL_RV_Surface *surf = (VL_RV_Surface *)surface;
	return surf->w * surf->h;
}

static void VL_RV_GetSurfaceDimensions(void *surface, int *w, int *h)
{
	VL_RV_Surface *surf = (VL_RV_Surface *)surface;
	if (w)
		*w = surf->w;
	if (h)
		*h = surf->h;
}

static int vl_rv_paletteDirty = 1;

static void VL_RV_RefreshPaletteAndBorderColor(void *screen)
{
	(void)screen;
	/* vl_emuegavgaadapter holds the state; applied on the next present
	 * (right after fb_present per hal.h's glitch-free-fade note). */
	vl_rv_paletteDirty = 1;
}

static int VL_RV_SurfacePGet(void *surface, int x, int y)
{
	VL_RV_Surface *surf = (VL_RV_Surface *)surface;
	return surf->data[y * surf->w + x];
}

static void VL_RV_SurfaceRect(void *dst_surface, int x, int y, int w, int h, int colour)
{
	VL_RV_Surface *surf = (VL_RV_Surface *)dst_surface;
	for (int _y = y; _y < y + h; ++_y)
		memset(surf->data + _y * surf->w + x, colour, w);
}

static void VL_RV_SurfaceRect_PM(void *dst_surface, int x, int y, int w, int h, int colour, int mapmask)
{
	mapmask &= 0xF;
	colour &= mapmask;

	VL_RV_Surface *surf = (VL_RV_Surface *)dst_surface;
	for (int _y = y; _y < y + h; ++_y)
		for (int _x = x; _x < x + w; ++_x)
		{
			uint8_t *p = surf->data + _y * surf->w + _x;
			*p &= ~mapmask;
			*p |= colour;
		}
}

static void VL_RV_SurfaceToSurface(void *src_surface, void *dst_surface, int x, int y, int sx, int sy, int sw, int sh)
{
	VL_RV_Surface *surf = (VL_RV_Surface *)src_surface;
	VL_RV_Surface *dest = (VL_RV_Surface *)dst_surface;
	for (int _y = sy; _y < sy + sh; ++_y)
		memcpy(dest->data + (_y - sy + y) * dest->w + x,
			surf->data + _y * surf->w + sx, sw);
}

static void VL_RV_SurfaceToSelf(void *surface, int x, int y, int sx, int sy, int sw, int sh)
{
	VL_RV_Surface *srf = (VL_RV_Surface *)surface;
	bool directionY = sy > y;

	if (directionY)
	{
		for (int yi = 0; yi < sh; ++yi)
			memmove(srf->data + ((yi + y) * srf->w + x),
				srf->data + ((sy + yi) * srf->w + sx), sw);
	}
	else
	{
		for (int yi = sh - 1; yi >= 0; --yi)
			memmove(srf->data + ((yi + y) * srf->w + x),
				srf->data + ((sy + yi) * srf->w + sx), sw);
	}
}

static void VL_RV_UnmaskedToSurface(void *src, void *dst_surface, int x, int y, int w, int h)
{
	VL_RV_Surface *surf = (VL_RV_Surface *)dst_surface;
	VL_UnmaskedToPAL8(src, surf->data, x, y, surf->w, w, h);
}

static void VL_RV_UnmaskedToSurface_PM(void *src, void *dst_surface, int x, int y, int w, int h, int mapmask)
{
	VL_RV_Surface *surf = (VL_RV_Surface *)dst_surface;
	VL_UnmaskedToPAL8_PM(src, surf->data, x, y, surf->w, w, h, mapmask);
}

static void VL_RV_MaskedToSurface(void *src, void *dst_surface, int x, int y, int w, int h)
{
	VL_RV_Surface *surf = (VL_RV_Surface *)dst_surface;
	VL_MaskedToPAL8(src, surf->data, x, y, surf->w, w, h);
}

static void VL_RV_MaskedBlitToSurface(void *src, void *dst_surface, int x, int y, int w, int h)
{
	VL_RV_Surface *surf = (VL_RV_Surface *)dst_surface;
	VL_MaskedBlitClipToPAL8(src, surf->data, x, y, surf->w, w, h, surf->w, surf->h);
}

static void VL_RV_BitToSurface(void *src, void *dst_surface, int x, int y, int w, int h, int colour)
{
	VL_RV_Surface *surf = (VL_RV_Surface *)dst_surface;
	VL_1bppToPAL8(src, surf->data, x, y, surf->w, w, h, colour);
}

static void VL_RV_BitToSurface_PM(void *src, void *dst_surface, int x, int y, int w, int h, int colour, int mapmask)
{
	VL_RV_Surface *surf = (VL_RV_Surface *)dst_surface;
	VL_1bppToPAL8_PM(src, surf->data, x, y, surf->w, w, h, colour, mapmask);
}

static void VL_RV_BitXorWithSurface(void *src, void *dst_surface, int x, int y, int w, int h, int colour)
{
	VL_RV_Surface *surf = (VL_RV_Surface *)dst_surface;
	VL_1bppXorWithPAL8(src, surf->data, x, y, surf->w, w, h, colour);
}

static void VL_RV_BitBlitToSurface(void *src, void *dst_surface, int x, int y, int w, int h, int colour)
{
	VL_RV_Surface *surf = (VL_RV_Surface *)dst_surface;
	VL_1bppBlitToPAL8(src, surf->data, x, y, surf->w, w, h, colour);
}

static void VL_RV_BitInvBlitToSurface(void *src, void *dst_surface, int x, int y, int w, int h, int colour)
{
	VL_RV_Surface *surf = (VL_RV_Surface *)dst_surface;
	VL_1bppInvBlitClipToPAL8(src, surf->data, x, y, surf->w, w, h, surf->w, surf->h, colour);
}

static void VL_RV_ScrollSurface(void *surface, int x, int y)
{
	VL_RV_Surface *surf = (VL_RV_Surface *)surface;
	int dx = 0, dy = 0, sx = 0, sy = 0;
	int w = surf->w - CK_Cross_max(x, -x), h = surf->h - CK_Cross_max(y, -y);
	if (x > 0)
	{
		dx = 0;
		sx = x;
	}
	else
	{
		dx = -x;
		sx = 0;
	}
	if (y > 0)
	{
		dy = 0;
		sy = y;
	}
	else
	{
		dy = -y;
		sy = 0;
	}
	VL_RV_SurfaceToSelf(surface, dx, dy, sx, sy, w, h);
}

/* ------------------------------------------------------------ present -- */

static void VL_RV_ApplyPalette(void)
{
	static uint8_t pal[256][3];
	for (int i = 0; i < 256; i++)
	{
		int ega = vl_emuegavgaadapter.palette[i & 0xF] & 0xF;
		pal[i][0] = VL_EGARGBColorTable[ega][0];
		pal[i][1] = VL_EGARGBColorTable[ega][1];
		pal[i][2] = VL_EGARGBColorTable[ega][2];
	}
	palette_set((const uint8_t(*)[3])pal);
	vl_rv_paletteDirty = 0;
}

static void VL_RV_Present(void *surface, int scrlX, int scrlY, bool singleBuffered)
{
	(void)singleBuffered;
	VL_RV_Surface *surf = (VL_RV_Surface *)surface;

	static int announced = 0;
	if (!announced)
	{
		announced = 1;
		RVK_Beacon(4);
	}

	RVK_TimerPump();

	uint8_t *fb = fb_backbuffer();
	int fbw = fb_width(), fbh = fb_height();
	int w = vl_rv_screenWidth ? vl_rv_screenWidth : VL_EGAVGA_GFX_WIDTH;
	int h = vl_rv_screenHeight ? vl_rv_screenHeight : VL_EGAVGA_GFX_HEIGHT;
	if (w > fbw)
		w = fbw;
	if (h > fbh)
		h = fbh;
	int yoff = (fbh - h) / 2;

	/* Clamp the pan so a bad scroll can never walk off the surface. */
	if (scrlX < 0)
		scrlX = 0;
	if (scrlY < 0)
		scrlY = 0;
	if (scrlX > surf->w - w)
		scrlX = surf->w - w;
	if (scrlY > surf->h - h)
		scrlY = surf->h - h;

	/* Letterbox bands in the EGA border color (index is 0..15; the HAL
	 * palette repeats every 16 so the raw index is fine). */
	uint8_t border = vl_emuegavgaadapter.bordercolor & 0xF;
	memset(fb, border, yoff * fbw);
	memset(fb + (yoff + h) * fbw, border, (fbh - h - yoff) * fbw);

	const uint8_t *src = surf->data + scrlY * surf->w + scrlX;
	uint8_t *dst = fb + yoff * fbw;
	for (int y = 0; y < h; y++, src += surf->w, dst += fbw)
		memcpy(dst, src, w);

	fb_present();
	if (vl_rv_paletteDirty)
		VL_RV_ApplyPalette(); /* right after the flip: glitch-free fades */
}

/* ------------------------------------------------------------- timing -- */

/* One EGA VBL = 1/60 s wall time; tics tick at 70 Hz off the t0 service.
 * Wait by real time, pumping the sound service and the deferred flip. */
static void VL_RV_WaitVBLs(int vbls)
{
	RVK_TimerPump();
	fb_flip_poll();
	if (vbls <= 0)
		return;
	uint32_t end = sys_ticks_us() + (uint32_t)vbls * (1000000u / 60u);
	while ((int32_t)(end - sys_ticks_us()) > 0)
	{
		RVK_TimerPump();
		fb_flip_poll();
		sys_delay_us(500);
	}
}

static int VL_RV_GetActiveBufferId(void *surface)
{
	(void)surface;
	return 0;
}

static int VL_RV_GetNumBuffers(void *surface)
{
	(void)surface;
	return 1;
}

static void VL_RV_SyncBuffers(void *surface)
{
	(void)surface;
}

static void VL_RV_UpdateRect(void *surface, int x, int y, int w, int h)
{
	(void)surface;
	(void)x;
	(void)y;
	(void)w;
	(void)h;
}

static void VL_RV_FlushParams(void)
{
	/* fullscreen/aspect/border toggles are meaningless on the console */
}

VL_Backend vl_rvstack_backend = {
	/*.setVideoMode =*/&VL_RV_SetVideoMode,
	/*.createSurface =*/&VL_RV_CreateSurface,
	/*.destroySurface =*/&VL_RV_DestroySurface,
	/*.getSurfaceMemUse =*/&VL_RV_GetSurfaceMemUse,
	/*.getSurfaceDimensions =*/&VL_RV_GetSurfaceDimensions,
	/*.refreshPaletteAndBorderColor =*/&VL_RV_RefreshPaletteAndBorderColor,
	/*.surfacePGet =*/&VL_RV_SurfacePGet,
	/*.surfaceRect =*/&VL_RV_SurfaceRect,
	/*.surfaceRect_PM =*/&VL_RV_SurfaceRect_PM,
	/*.surfaceToSurface =*/&VL_RV_SurfaceToSurface,
	/*.surfaceToSelf =*/&VL_RV_SurfaceToSelf,
	/*.unmaskedToSurface =*/&VL_RV_UnmaskedToSurface,
	/*.unmaskedToSurface_PM =*/&VL_RV_UnmaskedToSurface_PM,
	/*.maskedToSurface =*/&VL_RV_MaskedToSurface,
	/*.maskedBlitToSurface =*/&VL_RV_MaskedBlitToSurface,
	/*.bitToSurface =*/&VL_RV_BitToSurface,
	/*.bitToSurface_PM =*/&VL_RV_BitToSurface_PM,
	/*.bitXorWithSurface =*/&VL_RV_BitXorWithSurface,
	/*.bitBlitToSurface =*/&VL_RV_BitBlitToSurface,
	/*.bitInvBlitToSurface =*/&VL_RV_BitInvBlitToSurface,
	/*.scrollSurface =*/&VL_RV_ScrollSurface,
	/*.present =*/&VL_RV_Present,
	/*.getActiveBufferId =*/&VL_RV_GetActiveBufferId,
	/*.getNumBuffers =*/&VL_RV_GetNumBuffers,
	/*.syncBuffers =*/&VL_RV_SyncBuffers,
	/*.updateRect =*/&VL_RV_UpdateRect,
	/*.flushParams =*/&VL_RV_FlushParams,
	/*.waitVBLs =*/&VL_RV_WaitVBLs};

VL_Backend *VL_Impl_GetBackend()
{
	return &vl_rvstack_backend;
}
