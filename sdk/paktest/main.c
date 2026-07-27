// paktest — pak auto-load probe, now an ON-SCREEN HARDWARE diagnostic.
//
// The feature (pak_bind_named -> target_dataslot_openfile) passes in the sim
// but has NEVER worked on the real Pocket. The open question is the firmware's
// PATH SEMANTICS for openfile: what directory the name resolves against
// (Saves/<platform>/? the slot's current-file directory? core Assets?) and
// whether an empty browse slot has a directory context at all. The sim TB
// serves any name (our assumption, not Analogue's) — hence the sim/device gap.
//
// This probe answers it empirically: it tries every plausible variant against
// the real firmware and PRINTS each result on screen (no console on device).
// Run it as a normal game .bin (Assets/riscv_stack/common/paktest.bin) with
// paktest.pak alongside it. Run TWICE:
//   1st: launch paktest.bin directly (Pak slot never picked)  <- the real case
//   2nd: pick paktest.pak in the Pak slot first, then relaunch and compare
//
// Reading a row:  ok  = openfile succeeded AND the pakfs magic read back
//                 o=N = openfile refused with APF error N (2 = not found)
//                 RD  = openfile said OK but the read-back was garbage
// Diag mirror for the sim: 0x0FACxxxx, 0x0FAC00F0 = probe complete.
//
// SPDX-License-Identifier: BSD-2-Clause
#include "hal.h"
#include <string.h>
#include <system.h>
#include "../font8x8_basic.h"

#define D(x) sys_diag(0x0FAC0000u | (x))
#define PAKFS_MAGIC 0x464B4150u          // "PAKF"
#define OFF 0x02100000u                  // landing zone above the game region

static void text(uint8_t *fb, int W, int H, const char *s, int x0, int y0, uint8_t col)
{
	for (int ci = 0; s[ci]; ci++) {
		const char *g = font8x8_basic[(uint8_t)s[ci] & 0x7F];
		for (int ry = 0; ry < 8; ry++)
			for (int rx = 0; rx < 8; rx++)
				if ((g[ry] >> rx) & 1) {
					int x = x0 + ci * 8 + rx, y = y0 + ry;
					if (x >= 0 && x < W && y >= 0 && y < H)
						fb[y * W + x] = col;
				}
	}
}

// one variant: openfile on `slot` with `path`, then verify a REAL read through
// the bound handle. 0 = ok; >0 = openfile err code; -1 = opened but read bad.
static int try_variant(int slot, const char *path)
{
	int r = pak_bind_named_slot(slot, path);
	if (r != 0)
		return -r;                        // positive APF err (8 = watchdog)
	// poison the landing zone so a stale hit can't fake success
	volatile uint32_t *lz = (volatile uint32_t *)(0x40000000u + OFF);
	lz[0] = 0xDEADBEEF;
	flush_cpu_dcache_range((void *)(uintptr_t)lz, 16);
	if (pak_slot_read(OFF, 0, 16) != 0)
		return -1;
	const uint32_t *h = (const uint32_t *)(0x40000000u + OFF);
	return (h[0] == PAKFS_MAGIC) ? 0 : -1;
}

typedef struct { const char *label; int slot; const char *path; } variant_t;

static const variant_t V[] = {
	{ "s1 paktest.pak",                 1, "paktest.pak" },
	{ "s1 common/paktest.pak",          1, "common/paktest.pak" },
	{ "s1 riscv_stack/common/..",       1, "riscv_stack/common/paktest.pak" },
	{ "s1 Assets/riscv_stack/c../..",   1, "Assets/riscv_stack/common/paktest.pak" },
	{ "s1 /Assets/riscv_stack/c../..",  1, "/Assets/riscv_stack/common/paktest.pak" },
	{ "s0 paktest.pak (game dir ctx)",  0, "paktest.pak" },
	{ "s3 paktest.pak (save slot)",     3, "paktest.pak" },
};
#define NV (int)(sizeof V / sizeof V[0])

int main(void)
{
	sys_init();
	D(0x001);

	const int W = fb_width(), H = fb_height();
	uint8_t *fb = fb_backbuffer();
	memset(fb, 0, (size_t)W * H);
	text(fb, W, H, "PAK OPENFILE PROBE", 8, 8, 0xFF);
	fb_present();

	int first_ok = -1;
	for (int i = 0; i < NV; i++) {
		int r = try_variant(V[i].slot, V[i].path);
		fb = fb_backbuffer();
		char line[48];
		int n = 0;
		for (const char *c = V[i].label; *c && n < 32; c++) line[n++] = *c;
		while (n < 33) line[n++] = ' ';
		if (r == 0)     { line[n++] = 'o'; line[n++] = 'k'; }
		else if (r < 0) { line[n++] = 'R'; line[n++] = 'D'; }
		else            { line[n++] = 'o'; line[n++] = '=';
		                  line[n++] = (char)('0' + (r % 10)); }
		line[n] = 0;
		text(fb, W, H, line, 8, 24 + i * 12, (r == 0) ? 0x1C : 0xE0);
		fb_present();
		D(0x100u | ((uint32_t)((r < 0 ? 0xF : r) & 0xF) << 4) | (uint32_t)i);
		if (r == 0 && first_ok < 0) first_ok = i;
	}

	fb = fb_backbuffer();
	if (first_ok >= 0) {
		char msg[24];
		memcpy(msg, "WORKS: variant #0", 18);
		msg[16] = (char)('0' + first_ok);
		text(fb, W, H, msg, 8, 24 + NV * 12 + 8, 0x1C);
	} else {
		text(fb, W, H, "ALL VARIANTS FAILED", 8, 24 + NV * 12 + 8, 0xE0);
	}
	text(fb, W, H, "SELECT+START = exit", 8, H - 16, 0x92);
	fb_present();
	D(0x0F0);

	for (;;) {
		input_poll();
		uint32_t b = input_buttons(0);
		if ((b & HAL_BTN_SELECT) && (b & HAL_BTN_START))
			sys_exit();
		sys_delay_us(16667);
	}
}
