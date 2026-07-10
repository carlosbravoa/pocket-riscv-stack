// Bootloader (the only code in ROM) — turns the core into a console.
//
// Brings up the SoC (sys_init), then polls the APF "Game" data slot. When the
// user picks a game.bin, it is pulled into DRAM at its execution address and
// jumped to. Games are ordinary files on the SD card built with sdk/ — iterating
// a game never touches Quartus or this ROM.
//
// SPDX-License-Identifier: BSD-2-Clause

#include <stdint.h>
#include <generated/csr.h>      // boot_skip flag (bootloader is BSP, not a game)
#include "hal.h"
#include "font8x8_basic.h"

#define BG    0x02          // rgb332: very dark blue
#define INK   0xFF          // white
#define DIM   0x6E          // grey-green
#define ACC   0xF8          // red-ish accent

static int W, H;

static void text(uint8_t *fb, const char *s, int x0, int y0, int scale, uint8_t col)
{
	for (int ci = 0; s[ci]; ci++) {
		const char *g = font8x8_basic[(uint8_t)s[ci] & 0x7F];
		for (int ry = 0; ry < 8; ry++)
			for (int rx = 0; rx < 8; rx++)
				if ((g[ry] >> rx) & 1)
					for (int sy = 0; sy < scale; sy++)
						for (int sx = 0; sx < scale; sx++) {
							int x = x0 + (ci * 8 + rx) * scale + sx;
							int y = y0 + ry * scale + sy;
							if (x >= 0 && x < W && y >= 0 && y < H)
								fb[y * W + x] = col;
						}
	}
}

static void center(uint8_t *fb, const char *s, int y, int scale, uint8_t col)
{
	int len = 0;
	while (s[len]) len++;
	text(fb, s, (W - len * 8 * scale) / 2, y, scale, col);
}

int main(void)
{
	sys_init();
	W = fb_width(); H = fb_height();

	// After a game called sys_exit(), core_top holds boot_skip: show the picker
	// instead of relaunching the still-selected game. Picking any game clears it.
	sys_diag(0xB0070001);                     // boot stage: picker entered
	int skip = main_boot_skip_read();
	const char *status = skip ? "GAME EXITED - PICK A GAME (POCKET MENU)"
	                          : "INSERT GAME.BIN (POCKET MENU)";
	uint8_t status_col = DIM;
	uint32_t frame = 0;

	for (;;) {
		uint8_t *fb = fb_backbuffer();
		for (int i = 0; i < W * H; i++)
			fb[i] = BG;

		center(fb, "RISC-V STACK", 56, 3, INK);
		center(fb, "CONSOLE BOOTLOADER", 86, 1, DIM);
		center(fb, status, 140, 1, status_col);
		// heartbeat so a hung load is visually distinct from a running poll
		fb[(H - 8) * W + 8 + ((frame >> 3) & 31)] = INK;

		fb_present();
		frame++;

		// Check the Game slot every 8 frames (~7/s; a pick posts the size —
		// checking often keeps game start snappy, and the empty-slot probe is
		// cheap). Suppressed after a game exit until the user picks again
		// (which clears boot_skip in core_top and reboots us anyway).
		skip = main_boot_skip_read();
		if (!skip && (frame & 7) == 1) {
			pak_file_t game;
			int r = pak_load_game(&game);
			if (r == 0 && game.size > 0) {
				for (int i = 0; i < W * H; i++) fb_backbuffer()[i] = BG;
				center(fb_backbuffer(), "STARTING...", 140, 1, INK);
				fb_present();
				sys_diag(0xB0070004);     // boot stage: launching game
				pak_run_game(&game);
				// Game returned: back to the picker.
				status = "GAME EXITED - PICK AGAIN";
				status_col = ACC;
			} else if (r == -2) {
				status = "LOAD ERROR - REPICK OR RESET";
				status_col = ACC;
			}
		}
	}
	return 0;
}
