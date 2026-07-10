// Demo app (Layer 3) — uses ONLY the HAL (hal.h), never csr.h or a raw address.
//
// Same animated XOR plaid + bouncing box as Stage 3, but now drawn into the HAL's
// back buffer and published with fb_present() — double-buffered, so no tearing and
// no flicker. This is what "porting an app" looks like: it talks to the HAL and
// knows nothing about the SoC underneath.
//
// SPDX-License-Identifier: BSD-2-Clause

#include <stdint.h>
#include "hal.h"

#define AFRAME 800                          // stereo frames per 60 Hz tick (48000/60)
static int16_t abuf[2 * AFRAME];

int main(void)
{
	sys_init();
	audio_stream_open(48000);

	const int W = fb_width(), H = fb_height();
	const int BS = 28;
	int bx = 40, by = 40, dx = 3, dy = 2;
	int beep = 0;                           // beep frames remaining
	int beep_half = 27;                     // half-period in samples (~889 Hz)
	uint32_t phase = 0;
	uint32_t frame = 0;

	// If the user picked a pak file that holds at least one full 8bpp frame,
	// use it as the background instead of the generated plaid.
	pak_file_t pak;
	int have_bg = (pak_open("background", &pak) == 0 && pak.size >= (uint32_t)(W * H));
	uint32_t prev_btn = 0;

	for (;;) {
		uint8_t  *fb  = fb_backbuffer();
		uint32_t *fbw = (uint32_t *)fb;

		// Input: d-pad steers the box (overrides the bounce), A recenters it,
		// X (edge) re-pulls the pak (picking a new file from the menu updates
		// the slot; X makes it the new background without a reboot).
		input_poll();
		uint32_t btn = input_buttons(0);
		int steering = btn & (HAL_BTN_UP | HAL_BTN_DOWN | HAL_BTN_LEFT | HAL_BTN_RIGHT);
		if ((btn & HAL_BTN_X) && !(prev_btn & HAL_BTN_X))
			have_bg = (pak_open("background", &pak) == 0 && pak.size >= (uint32_t)(W * H));
		prev_btn = btn;

		if (have_bg) {
			// Background = first W*H bytes of the pak file (raw rgb332 frame).
			const uint32_t *src = (const uint32_t *)pak.base;
			for (int i = 0; i < W * H / 4; i++)
				fbw[i] = src[i];
		} else {
			// Animated XOR plaid, 4 pixels/word. (uint32_t casts: a plain uint8_t
			// promotes to signed int, and <<24 into the sign bit is UB.)
			for (int y = 0; y < H; y++) {
				for (int xw = 0; xw < W / 4; xw++) {
					int x = xw * 4;
					fbw[y * (W / 4) + xw] =
						  (uint32_t)(uint8_t)((x     ^ y) + frame)
						| (uint32_t)(uint8_t)(((x+1) ^ y) + frame) << 8
						| (uint32_t)(uint8_t)(((x+2) ^ y) + frame) << 16
						| (uint32_t)(uint8_t)(((x+3) ^ y) + frame) << 24;
				}
			}
		}
		// Bouncing white box.
		for (int y = 0; y < BS; y++)
			for (int x = 0; x < BS; x++)
				fb[(by + y) * W + (bx + x)] = 0xFF;

		// Movement: steered while the d-pad is held, bouncing otherwise.
		if (steering) {
			if (btn & HAL_BTN_LEFT)  bx -= 3;
			if (btn & HAL_BTN_RIGHT) bx += 3;
			if (btn & HAL_BTN_UP)    by -= 3;
			if (btn & HAL_BTN_DOWN)  by += 3;
		} else {
			bx += dx; by += dy;
		}
		if (btn & HAL_BTN_A) {
			bx = (W - BS) / 2; by = (H - BS) / 2;
			beep = 3; beep_half = 54;       // lower blip (~444 Hz) on recenter
		}
		// Bounce with CLAMPING: flipping the sign alone lets the position overshoot
		// (e.g. bx = -2 when the step doesn't divide the travel) and the box would
		// be drawn out of bounds — off the left edge that's a write below the
		// framebuffer itself. Wall hits beep (classic pong).
		int hit = 0;
		if (bx < 0)          { bx = 0;      dx = -dx; hit = 1; }
		else if (bx > W - BS){ bx = W - BS; dx = -dx; hit = 1; }
		if (by < 0)          { by = 0;      dy = -dy; hit = 1; }
		else if (by > H - BS){ by = H - BS; dy = -dy; hit = 1; }
		if (hit) { beep = 3; beep_half = 27; }
		frame++;

		// One display frame of audio: square-wave beep or silence. The blocking
		// FIFO write paces us; drawing + audio both fit comfortably in a frame.
		for (int i = 0; i < AFRAME; i++) {
			int16_t v = 0;
			if (beep)
				v = ((phase / beep_half) & 1) ? 5000 : -5000;
			phase++;
			abuf[2 * i] = abuf[2 * i + 1] = v;
		}
		if (beep) beep--;
		audio_stream_write(abuf, AFRAME);

		fb_present();       // tear-free flip, paced to the display
	}
	return 0;
}
