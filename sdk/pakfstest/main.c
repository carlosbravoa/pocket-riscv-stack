// pakfstest — portlib exerciser for the full-system sim: mounts a pakfs pak
// (served by the testbench on the Pak slot), verifies file contents, then
// runs a few frames through the SDL-lite shim. Diag codes tell the TB how
// far we got; 0xBAD in the low bits = the step above it failed.
//
// SPDX-License-Identifier: BSD-2-Clause
#include "hal.h"
#include "pakfs.h"
#include "sdl_lite.h"
#include <string.h>

#define D(x) sys_diag(0x9AC00000u | (x))

int main(void)
{
	sys_init();
	D(0x001);

	// ---- pakfs ----
	int r = pakfs_mount();
	D(0x100 | ((unsigned)(r < 0 ? 0x80 | -r : r) & 0xFF));
	if (r != 0) { D(0xBAD); sys_exit(); }
	D(0x200 | (unsigned)pakfs_nfiles());

	uint32_t sz = 0;
	const char *txt = pakfs_data("readme.txt", &sz);
	D((txt && sz == 11 && !memcmp(txt, "hello pakfs", 11)) ? 0x301 : 0xBAD);

	pakfs_file_t f;
	r = pakfs_open("data/sprites.bin", &f);
	D(r == 0 ? 0x302 : 0xBAD);
	uint8_t buf[16];
	pakfs_seek(&f, 4096, PAKFS_SEEK_SET);
	uint32_t n = pakfs_read(buf, 1, sizeof buf, &f);
	int ok = (n == sizeof buf);
	for (unsigned i = 0; ok && i < sizeof buf; i++)
		if (buf[i] != (uint8_t)(((4096 + i) * 7) & 0xFF)) ok = 0;
	D(ok ? 0x303 : 0xBAD);
	pakfs_seek(&f, -8, PAKFS_SEEK_END);
	D(pakfs_tell(&f) == f.size - 8 ? 0x304 : 0xBAD);
	D(pakfs_open("missing.dat", &f) != 0 ? 0x305 : 0xBAD);

	// ---- SDL-lite: 320x200 mode, palette, blit, a few flips ----
	SDL_Init(SDL_INIT_VIDEO);
	SDL_Surface *s = SDL_SetVideoMode(320, 200, 8, 0);
	D(s ? 0x401 : 0xBAD);
	SDL_Color pal[256];
	for (int i = 0; i < 256; i++)
		pal[i] = (SDL_Color){(Uint8)i, (Uint8)(255 - i), 128, 0};
	SDL_SetColors(s, pal, 0, 256);

	SDL_Surface *spr = SDL_CreateRGBSurface(0, 16, 16, 8, 0, 0, 0, 0);
	D(spr ? 0x402 : 0xBAD);
	memset(spr->pixels, 42, 16 * 16);
	SDL_FillRect(s, 0, 7);
	SDL_Rect dst = { 100, 90, 0, 0 };
	SDL_BlitSurface(spr, 0, s, &dst);
	for (int i = 0; i < 3; i++)
		SDL_Flip(s);
	uint8_t *px = (uint8_t *)s->pixels;
	D((px[95 * s->pitch + 105] == 42 && px[0] == 7) ? 0x403 : 0xBAD);
	D(SDL_GetTicks() > 0 ? 0x404 : 0xBAD);

	D(0x0F0);                           // done
	sys_exit();
}
