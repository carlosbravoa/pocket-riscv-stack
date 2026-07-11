// PONG (single player, against the wall) — the SDK's example game.
//
// A complete small game showing every part of the HAL working together:
//   - the frame loop:  input_poll -> update -> draw -> audio -> fb_present
//   - input:           d-pad + buttons with edge detection
//   - video:           full-frame draw into the back buffer, 8x8 font text
//   - audio:           square-wave SFX pushed one display-frame at a time
//
// Rules: the ball bounces off the top/left/right walls; you defend the bottom
// with the paddle. Every save scores a point and the ball gradually speeds up.
// Where the ball hits the paddle adds english. Three misses = game over.
//
// Build: `make` in this directory -> pong.bin -> copy to the Pocket SD card,
// pick it in the core's Game slot. That's the whole deployment.
//
// SPDX-License-Identifier: BSD-2-Clause

#include <stdint.h>
#include "hal.h"
#include "font8x8_basic.h"

// ---------------------------------------------------------------------------
// Tiny drawing helpers (rgb332 framebuffer, 1 byte per pixel)
// ---------------------------------------------------------------------------

static int W, H;
static uint8_t *fb;                       // current back buffer, set each frame

#define C_BG     0x01                     // near-black blue
#define C_WALL   0x6D                     // grey
#define C_BALL   0xFF                     // white
#define C_PADDLE 0x1F                     // cyan
#define C_TEXT   0xFF
#define C_DIM    0x49

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
					rect(x0 + (ci * 8 + rx) * scale,
					     y0 + ry * scale, scale, scale, col);
	}
}

static void center(const char *s, int y, int scale, uint8_t col)
{
	int len = 0;
	while (s[len]) len++;
	text(s, (W - len * 8 * scale) / 2, y, scale, col);
}

static void unum(char *dst, unsigned v)   // minimal utoa for the score
{
	char tmp[10]; int n = 0;
	do { tmp[n++] = '0' + v % 10; v /= 10; } while (v);
	while (n) *dst++ = tmp[--n];
	*dst = 0;
}

// ---------------------------------------------------------------------------
// Sound: one square-wave "voice". Events set pitch+duration; every frame we
// synthesize exactly one display frame of samples (48000/60 = 800) and push it.
// audio_stream_write() blocks on the hardware FIFO, so audio also paces the
// game if video ever runs ahead.
// ---------------------------------------------------------------------------

#define AFRAME 800
static int16_t abuf[2 * AFRAME];
static int beep_frames;                   // remaining duration in display frames
static int beep_half;                     // half period in samples (pitch)
static uint32_t beep_phase;

static void beep(int half_period, int frames)
{
	beep_half = half_period; beep_frames = frames; beep_phase = 0;
}

static void audio_frame(void)
{
	for (int i = 0; i < AFRAME; i++) {
		int16_t v = 0;
		if (beep_frames)
			v = ((beep_phase / beep_half) & 1) ? 5000 : -5000;
		beep_phase++;
		abuf[2 * i] = abuf[2 * i + 1] = v;
	}
	if (beep_frames) beep_frames--;
	audio_stream_write(abuf, AFRAME);
}

#define SFX_PADDLE()  beep(20, 3)         // ~1.2 kHz, crisp
#define SFX_WALL()    beep(40, 2)         // ~600 Hz
#define SFX_MISS()    beep(120, 12)       // ~200 Hz, long
#define SFX_START()   beep(27, 4)

// ---------------------------------------------------------------------------
// Palette effects. The framebuffer holds rgb332-style indices; the hardware
// palette maps them to RGB888. We rebuild the whole palette from the identity
// mapping, scaled toward black (fades) or tinted red (miss flash).
// level: 0..16 brightness, redshift: 0..16 mix toward red.
// ---------------------------------------------------------------------------

static void palette_fx(int level, int redshift)
{
	static uint8_t pal[256][3];
	for (int i = 0; i < 256; i++) {
		int r = ((i >> 5) & 7) << 5;      // the identity rgb332 expansion
		int g = ((i >> 2) & 7) << 5;
		int b = (i & 3) << 6;
		r = r + ((255 - r) * redshift) / 16;              // tint toward red
		g = (g * (16 - redshift)) / 16;
		b = (b * (16 - redshift)) / 16;
		pal[i][0] = (uint8_t)((r * level) / 16);          // then fade
		pal[i][1] = (uint8_t)((g * level) / 16);
		pal[i][2] = (uint8_t)((b * level) / 16);
	}
	palette_set((const uint8_t (*)[3])pal);
}

// ---------------------------------------------------------------------------
// Game
// ---------------------------------------------------------------------------

#define WALL   4                          // wall thickness
#define PW     44                         // paddle size
#define PH     6
#define BS     6                          // ball size
#define PADDLE_Y (H - 14)

enum { ST_TITLE, ST_PLAY, ST_OVER };

int main(void)
{
	sys_init();
	audio_stream_open(48000);
	W = fb_width(); H = fb_height();

	int state = ST_TITLE;
	int px = 0;                           // paddle left x
	int bx = 0, by = 0, dx = 0, dy = 0;   // ball, in 1/8 px (fixed point <<3)
	unsigned score = 0, best = 0;
	int lives = 0;
	int fade = 16, flash = 0;             // palette effects (16 = normal)
	uint32_t prev = 0, frame = 0;

	// Persistent best score: pong owns Saves/riscv_stack/pong.sav (created on
	// first boot). Magic still guards against a fresh/garbled file.
	// save_diag: title-screen readout while saves are hardware-young.
	//   "SAV OK" opened / "SAV NEW" created / "SAV E<r> P<pak_err>" failed.
	struct rec { uint32_t magic, best; } *sav = 0;
	save_file_t savf;
	int save_r = save_open("pong", 64, &savf);
	unsigned save_perr = save_last_hw_err();
	if (save_r >= 0) {
		sav = (struct rec *)savf.base;
		if (sav->magic == 0x504F4E47)
			best = sav->best;
	}
	int commit_r = 99;                    // last save_commit result (99 = never)

	for (;;) {
		fb = fb_backbuffer();
		input_poll();
		uint32_t btn  = input_buttons(0);
		uint32_t edge = btn & ~prev;      // buttons newly pressed this frame
		prev = btn;

		// SELECT+START = exit to the console's game picker.
		if ((btn & HAL_BTN_SELECT) && (btn & HAL_BTN_START))
			sys_exit();

		// ------------------------------------------------ update
		if (state == ST_TITLE || state == ST_OVER) {
			if (edge & (HAL_BTN_A | HAL_BTN_START)) {
				score = 0; lives = 3;
				px = (W - PW) / 2;
				bx = (W / 2) << 3; by = (H / 2) << 3;
				dx = 12; dy = -20;        // 1.5 px/frame, 2.5 px/frame
				state = ST_PLAY;
				fade = 0;                 // palette fade-in from black
				SFX_START();
			}
		} else {
			// paddle
			if (btn & HAL_BTN_LEFT)  px -= 4;
			if (btn & HAL_BTN_RIGHT) px += 4;
			if (px < WALL) px = WALL;
			if (px > W - WALL - PW) px = W - WALL - PW;

			// ball
			bx += dx; by += dy;
			int x = bx >> 3, y = by >> 3;
			if (x <= WALL)               { bx = WALL << 3;            dx = -dx; SFX_WALL(); }
			if (x >= W - WALL - BS)      { bx = (W - WALL - BS) << 3; dx = -dx; SFX_WALL(); }
			if (y <= WALL)               { by = WALL << 3;            dy = -dy; SFX_WALL(); }

			// paddle save: ball bottom crosses the paddle line while overlapping it
			if (dy > 0 && y + BS >= PADDLE_Y && y + BS <= PADDLE_Y + PH
			    && x + BS > px && x < px + PW) {
				by = (PADDLE_Y - BS) << 3;
				dy = -dy;
				// english: hit position steers, center-hit straightens
				dx += ((x + BS / 2) - (px + PW / 2));
				if (dx >  40) dx =  40;
				if (dx < -40) dx = -40;
				score++;
				if ((score & 3) == 0 && dy > -48) dy -= 2;   // speed up every 4 saves
				SFX_PADDLE();
			}

			// miss
			if (y > H) {
				lives--;
				flash = 12;               // palette red flash, decays below
				SFX_MISS();
				if (lives <= 0) {
					if (score > best) {
						best = score;
						if (sav) {        // persist the new record to SD now
							sav->magic = 0x504F4E47;
							sav->best  = best;
							commit_r = save_commit(&savf);
						}
					}
					state = ST_OVER;
				} else {
					bx = (W / 2) << 3; by = (H / 2) << 3;
					dx = (frame & 1) ? 12 : -12; dy = -20;
				}
			}
		}

		// ------------------------------------------------ draw
		rect(0, 0, W, H, C_BG);
		rect(0, 0, W, WALL, C_WALL);                  // top
		rect(0, 0, WALL, H, C_WALL);                  // left
		rect(W - WALL, 0, WALL, H, C_WALL);           // right

		char num[12];
		if (state == ST_TITLE) {
			center("PONG", 60, 4, C_TEXT);
			if (best) {                   // restored record, visible at boot
				unum(num, best);
				center("BEST", 96, 1, C_DIM);
				text(num, W / 2 + 24, 96, 1, C_TEXT);
			}
			center("DEFEND THE BOTTOM WALL", 110, 1, C_DIM);
			if (frame & 32) center("PRESS A TO PLAY", 150, 1, C_TEXT);
			center("SELECT+START: EXIT TO PICKER", H - 34, 1, C_DIM);
			center("RISC-V STACK SDK EXAMPLE", H - 20, 1, C_DIM);
		} else {
			text("SCORE", 12, 8, 1, C_DIM);
			unum(num, score); text(num, 60, 8, 1, C_TEXT);
			text("LIVES", W - 100, 8, 1, C_DIM);
			unum(num, lives > 0 ? (unsigned)lives : 0); text(num, W - 52, 8, 1, C_TEXT);
			rect(px, PADDLE_Y, PW, PH, C_PADDLE);
			if (state == ST_PLAY)
				rect(bx >> 3, by >> 3, BS, BS, C_BALL);
			else {
				center("GAME OVER", 80, 3, C_TEXT);
				unum(num, best);
				center("BEST", 116, 1, C_DIM); text(num, W / 2 + 24, 116, 1, C_TEXT);
				{	// PROBE (v0.17.6): the host's own view of the file API.
					// GF2 = getfile(game slot): err + first window bytes —
					// readable path = buffer+byte-order OK; reversed/garbage
					// = byte order; blank = host can't reach our buffer.
					// OF2 = openfile replay of the UNTOUCHED host struct.
					// GF3 = getfile(save slot): how an unbound slot reports.
					static uint8_t gf2[20], gf3[8];
					static int e_gf2 = 99, e_of2 = 99, e_gf3 = 99;
					static int probed = 0;
					if (!probed) {
						probed = 1;
						e_gf2 = save_diag_getfile(2, gf2, sizeof gf2);
						if (e_gf2 >= 0 && e_gf2 <= 1)
							e_of2 = save_diag_openfile_raw(2);
						e_gf3 = save_diag_getfile(3, gf3, sizeof gf3);
					}
					char pl[34]; int pk = 0;
					pl[pk++]='G'; pl[pk++]='F'; pl[pk++]='2'; pl[pk++]=' ';
					pl[pk++]='E'; pl[pk++]='0'+(unsigned)(e_gf2<0?7:e_gf2)%10; pl[pk++]=' ';
					for (int i = 0; i < 16; i++) {
						char c = (char)gf2[i];
						pl[pk++] = (c >= 0x20 && c < 0x7F) ? c : '.';
					}
					pl[pk]=0;
					center(pl, 208, 1, C_DIM);
					char ql[26]; int qk = 0;
					ql[qk++]='O'; ql[qk++]='F'; ql[qk++]='2'; ql[qk++]=' ';
					ql[qk++]='E'; ql[qk++]='0'+(unsigned)(e_of2<0?7:e_of2)%10;
					ql[qk++]=' '; ql[qk++]='G'; ql[qk++]='F'; ql[qk++]='3';
					ql[qk++]=' '; ql[qk++]='E'; ql[qk++]='0'+(unsigned)(e_gf3<0?7:e_gf3)%10;
					ql[qk++]=' ';
					for (int i = 0; i < 4; i++) {
						char c = (char)gf3[i];
						ql[qk++] = (c >= 0x20 && c < 0x7F) ? c : '.';
					}
					ql[qk]=0;
					center(ql, 218, 1, C_DIM);
				}
				{	// save diagnostics (temporary, while saves are hardware-young)
					char d[24]; int k = 0;
					d[k++]='S'; d[k++]='A'; d[k++]='V'; d[k++]=' ';
					if (save_r == 0)      { d[k++]='O'; d[k++]='K'; }
					else if (save_r == 1) { d[k++]='N'; d[k++]='E'; d[k++]='W'; }
					else { d[k++]='E'; d[k++]='0'+(unsigned)(-save_r)%10;
					       d[k++]=' '; d[k++]='P'; d[k++]='0'+save_perr%10; }
					if (commit_r != 99) {
						d[k++]=' '; d[k++]='C';
						d[k++] = commit_r == 0 ? '+' : '0'+(unsigned)(-commit_r)%10;
					}
					d[k++]=' '; d[k++]='R';
					d[k++]='0'+(unsigned)save_restore_code()%10;
					d[k]=0;
					center(d, 228, 1, C_DIM);
				}
				if (frame & 32) center("PRESS A", 150, 1, C_TEXT);
			}
		}

		// ------------------------------------------------ sound + present
		audio_frame();
		fb_present();                     // tear-free flip, paced to 60 Hz

		// Palette effects, reloaded right after present (hidden-frame window):
		// fade-in after game start, red flash decaying after a miss.
		if (fade < 16 || flash > 0) {
			if (fade < 16) fade++;
			if (flash > 0) flash--;
			palette_fx(fade, flash);
		}
		frame++;
	}
	return 0;
}
