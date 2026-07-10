// FM DEMO (OPL3 fork) — a mini piano on the hardware FM synthesizer.
//
// The whole point of the fork in one file: the CPU never touches a sample.
// It writes OPL3 registers (a patch, then key-on/key-off) and the FPGA chip
// synthesizes and mixes into the audio path in hardware. Note there is no
// audio_stream_write()/audio_pump() anywhere in this game.
//
// Controls: d-pad UP/DOWN/LEFT/RIGHT + B/A/Y/X = one octave (C D E F G A B C).
// L1/R1 shift octaves. SELECT+START exits.
//
// SPDX-License-Identifier: BSD-2-Clause

#include <stdint.h>
#include <generated/csr.h>          // main_opl_dbg diagnostic (fork hardware)
#include "hal.h"
#include "font8x8_basic.h"

static int W, H;
static uint8_t *fb;

static void rect(int x, int y, int w, int h, uint8_t c)
{
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > W) w = W - x;
	if (y + h > H) h = H - y;
	for (int j = 0; j < h; j++)
		for (int i = 0; i < w; i++)
			fb[(y + j) * W + (x + i)] = c;
}

static void text(const char *s, int x0, int y0, int scale, uint8_t col)
{
	for (int ci = 0; s[ci]; ci++) {
		const char *g = font8x8_basic[(uint8_t)s[ci] & 0x7F];
		for (int ry = 0; ry < 8; ry++)
			for (int rx = 0; rx < 8; rx++)
				if ((g[ry] >> rx) & 1)
					rect(x0 + (ci * 8 + rx) * scale, y0 + ry * scale,
					     scale, scale, col);
	}
}

static void center(const char *s, int y, int scale, uint8_t col)
{
	int len = 0; while (s[len]) len++;
	text(s, (W - len * 8 * scale) / 2, y, scale, col);
}

// --------------------------------------------------------------------------
// OPL3 setup: one 2-op channel per key so releases ring out naturally.
// Operator slot offsets for channels 0..8 (bank 0), modulator/carrier pairs.
// --------------------------------------------------------------------------

static const uint8_t op_mod[9] = { 0x00,0x01,0x02,0x08,0x09,0x0A,0x10,0x11,0x12 };
static const uint8_t op_car[9] = { 0x03,0x04,0x05,0x0B,0x0C,0x0D,0x13,0x14,0x15 };

// fnums for one octave starting at C (block set per octave)
static const uint16_t fnum[8] = { 0x157,0x181,0x1B0,0x1CA,0x202,0x241,0x287,0x2AE };
static const char    *nname[8] = { "C","D","E","F","G","A","B","C+" };

static void fm_patch(int ch)
{
	// A warm electric-piano-ish 2-op patch.
	opl_write(0x20 + op_mod[ch], 0x01);   // mod: mult=1
	opl_write(0x40 + op_mod[ch], 0x18);   // mod: level (moderate FM depth)
	opl_write(0x60 + op_mod[ch], 0xF4);   // mod: fast attack, med decay
	opl_write(0x80 + op_mod[ch], 0x47);   // mod: sustain/release
	opl_write(0xE0 + op_mod[ch], 0x00);   // mod: sine
	opl_write(0x20 + op_car[ch], 0x01);   // car: mult=1
	opl_write(0x40 + op_car[ch], 0x00);   // car: full volume
	opl_write(0x60 + op_car[ch], 0xF2);   // car: fast attack, slow decay
	opl_write(0x80 + op_car[ch], 0x47);   // car: sustain/release
	opl_write(0xE0 + op_car[ch], 0x00);   // car: sine
	opl_write(0xC0 + ch, 0x30);           // L+R on, FM algorithm, no feedback
}

static void fm_key(int ch, int note, int block, int on)
{
	opl_write(0xA0 + ch, fnum[note] & 0xFF);
	opl_write(0xB0 + ch, (on ? 0x20 : 0x00) | ((block & 7) << 2)
	                     | ((fnum[note] >> 8) & 3));
}

int main(void)
{
	sys_init();
	W = fb_width(); H = fb_height();

	const hal_caps_t *caps = sys_caps();
	int have_fm = (caps->features & HAL_FEAT_FM) != 0;

	if (have_fm) {
		opl_write(0x105, 0x01);           // OPL3 mode (enables L/R routing)
		opl_write(0x01,  0x00);
		opl_write(0xBD,  0x00);           // no rhythm mode
		for (int ch = 0; ch < 8; ch++)
			fm_patch(ch);
	}

	// key bitmap in play order: C D E F G A B C+
	static const uint16_t keys[8] = {
		HAL_BTN_UP, HAL_BTN_DOWN, HAL_BTN_LEFT, HAL_BTN_RIGHT,
		HAL_BTN_B,  HAL_BTN_A,    HAL_BTN_Y,    HAL_BTN_X,
	};

	int block = 4;                        // middle octave
	uint32_t prev = 0, frame = 0;

	for (;;) {
		fb = fb_backbuffer();
		input_poll();
		uint32_t btn  = input_buttons(0);
		uint32_t edge = btn & ~prev;
		uint32_t rel  = prev & ~btn;
		prev = btn;

		if ((btn & HAL_BTN_SELECT) && (btn & HAL_BTN_START))
			sys_exit();
		if ((edge & HAL_BTN_L1) && block > 1) block--;
		if ((edge & HAL_BTN_R1) && block < 7) block++;

		if (have_fm)
			for (int n = 0; n < 8; n++) {
				if (edge & keys[n]) fm_key(n, n, block, 1);
				if (rel  & keys[n]) fm_key(n, n, block, 0);
			}

		// ---- draw: 8 keys light up while held
		rect(0, 0, W, H, 0x01);
		center("OPL3 FM PIANO", 20, 2, 0xFF);
		center(have_fm ? "HARDWARE FM - CPU WRITES REGISTERS ONLY"
		               : "NO FM HARDWARE (mainline core?)", 44, 1, 0x6E);
		for (int n = 0; n < 8; n++) {
			int x = 24 + n * 35, held = (btn & keys[n]) != 0;
			rect(x, 90, 30, 70, held ? 0xFC : 0x92);
			rect(x + 2, 92, 26, 66, held ? 0xFF : 0xDB);
			text(nname[n], x + 8, 120, 1, 0x00);
		}
		center("DPAD+BAYX: NOTES   L1/R1: OCTAVE", 180, 1, 0x6E);
		char oct[12] = "OCTAVE 0";
		oct[7] = '0' + block;
		center(oct, 196, 1, 0xFF);
		center("SELECT+START: EXIT", H - 16, 1, 0x49);

		// FM chain diagnostic: N=nonzero-sample-seen V=valid-seen W=write count.
		// Healthy after one keypress: N1 V1 W>=2 and W grows with every press.
		{
			uint32_t d = main_opl_dbg_read();
			char line[32] = "FM N0 V0 W0000";
			line[4]  = '0' + ((d >> 15) & 1);
			line[7]  = '0' + ((d >> 14) & 1);
			const char *hx = "0123456789ABCDEF";
			line[10] = hx[(d >> 12) & 3];
			line[11] = hx[(d >> 8) & 15];
			line[12] = hx[(d >> 4) & 15];
			line[13] = hx[d & 15];
			text(line, 8, H - 16, 1, 0x92);
		}

		fb_present();
		frame++;
	}
	return 0;
}
