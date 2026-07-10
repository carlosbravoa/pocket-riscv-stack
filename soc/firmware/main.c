// Demo app (Layer 3) — uses ONLY the HAL (hal.h), never csr.h or a raw address.
//
// Same animated XOR plaid + bouncing box as Stage 3, but now drawn into the HAL's
// back buffer and published with fb_present() — double-buffered, so no tearing and
// no flicker. This is what "porting an app" looks like: it talks to the HAL and
// knows nothing about the SoC underneath.
//
// SPDX-License-Identifier: BSD-2-Clause

#include <stdint.h>
#include <stdio.h>
#include "hal.h"

int main(void)
{
	sys_init();
	printf("[VexiiRiscv+CMO] sys_init done\n");   // TEMP: zicbom flush smoke test

	const int W = fb_width(), H = fb_height();
	const int BS = 28;
	int bx = 40, by = 40, dx = 3, dy = 2;
	uint32_t frame = 0;

	for (;;) {
		uint8_t  *fb  = fb_backbuffer();
		uint32_t *fbw = (uint32_t *)fb;

		// Input: d-pad steers the box (overrides the bounce), A recenters it.
		input_poll();
		uint32_t btn = input_buttons(0);
		int steering = btn & (HAL_BTN_UP | HAL_BTN_DOWN | HAL_BTN_LEFT | HAL_BTN_RIGHT);

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
		if (btn & HAL_BTN_A) { bx = (W - BS) / 2; by = (H - BS) / 2; }
		// Bounce with CLAMPING: flipping the sign alone lets the position overshoot
		// (e.g. bx = -2 when the step doesn't divide the travel) and the box would
		// be drawn out of bounds — off the left edge that's a write below the
		// framebuffer itself.
		if (bx < 0)          { bx = 0;      dx = -dx; }
		else if (bx > W - BS){ bx = W - BS; dx = -dx; }
		if (by < 0)          { by = 0;      dy = -dy; }
		else if (by > H - BS){ by = H - BS; dy = -dy; }
		frame++;

		fb_present();       // flip on vsync — no flicker
		if ((frame & 31) == 1) printf("[frame %u] presented\n", frame);  // TEMP
	}
	return 0;
}
