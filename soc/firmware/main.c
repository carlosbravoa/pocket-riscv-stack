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
	// The palette BRAM survives the SoC reset, so a game that set a custom palette
	// leaves it behind — the picker would render in the game's colors. Restore the
	// identity map so the bootloader always shows correct colors (v0.23 field fix).
	palette_reset();
	// Silence the OPL at boot: it lives in a clock domain the SoC reset doesn't
	// touch, so a game exited abruptly (the Pocket-menu "Exit to Menu" action
	// resets the core without giving the game a chance to key-off) can leave FM
	// notes ringing into the picker. Key-off every channel (no-op without FM).
	if (sys_caps()->features & HAL_FEAT_FM) {
		for (int ch = 0; ch < 9; ch++) { opl_write(0xB0 + ch, 0); opl_write(0x1B0 + ch, 0); }
		opl_write(0xBD, 0);
	}
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
		{	// Pak slot status (user request: show pak loading on screen)
			static uint32_t pak_kb; static int pak_chk;
			if ((frame & 31) == 2) {        // poll ~2x/s, off the game poll
				main_pak_id_write(1);
				main_pak_dtaddr_write(3);
				sys_delay_us(100);
				pak_kb = main_pak_size_read() >> 10;
				pak_chk = 1;
			}
			if (pak_chk) {
				char pl[28]; int k = 0;
				const char *s = pak_kb ? "PAK: " : "PAK: NONE (OPTIONAL)";
				for (; *s; s++) pl[k++] = *s;
				if (pak_kb) {
					char d[8]; int n = 0; uint32_t v = pak_kb;
					do { d[n++] = '0' + v % 10; v /= 10; } while (v);
					while (n) pl[k++] = d[--n];
					pl[k++] = ' '; pl[k++] = 'K'; pl[k++] = 'B';
				}
				pl[k] = 0;
				center(fb, pl, 158, 1, pak_kb ? INK : DIM);
			}
		}
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
				// Prominent LOADING screen: a game selection used to flash the
				// picker then jump with no feedback; the game's own pak pull then
				// left a black screen (field report). Draw a clear full-screen
				// LOADING and present it — sys_init's adoptive warm-start does NOT
				// clear the front page, so this stays on screen through the game's
				// pak load until the game presents its first frame.
				uint8_t *lb = fb_backbuffer();
				for (int i = 0; i < W * H; i++) lb[i] = BG;
				center(lb, "LOADING", 96, 4, INK);
				center(lb, "please wait", 150, 1, DIM);
				fb_present();
				fb_present();             // both pages -> LOADING survives the flip
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
