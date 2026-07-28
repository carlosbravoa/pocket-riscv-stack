/* vmxprobe2 — vmx fetch characterization on hardware (see main.c for round 1:
 * CPU beep clean, vmx tone garbled => DMA sample path suspect on silicon).
 *
 * Round 2 removes the re-kick variable and makes the fetch VISIBLE:
 *   BLUE    2 s: CPU-stream 440 Hz beep (control, no DMA)
 *   MAGENTA 2 s: ONE vmx_key_on of a 1-second 220 Hz square (96 KB, one-shot,
 *                never re-kicked)
 *   GREEN   2 s: the same buffer hardware-LOOPED by the voice
 *
 * During magenta/green, two markers per frame in the top letterbox rows:
 *   white column = vmx_pos(voice) mapped across the screen width
 *   grey column  = where the position SHOULD be from elapsed time
 * A healthy voice: white tracks grey. A starving/wedged fetch: white stalls,
 * jumps back, or crawls — photograph it if it looks interesting.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <stdint.h>
#include "hal.h"

#define N 48000

static int16_t wave[N];

static void fill_screen(uint8_t c)
{
	for (int i = 0; i < 2; i++) {
		uint8_t *fb = fb_backbuffer();
		int n = fb_width() * fb_height();
		for (int k = 0; k < n; k++)
			fb[k] = c;
		fb_present();
	}
}

static void pos_frame(uint8_t bg, uint32_t pos, uint32_t want)
{
	uint8_t *fb = fb_backbuffer();
	int w = fb_width(), h = fb_height();
	for (int k = 0; k < w * h; k++)
		fb[k] = bg;
	int xp = (int)((uint64_t)pos * (uint32_t)w / N);
	int xw = (int)((uint64_t)want * (uint32_t)w / N);
	if (xp >= w) xp = w - 1;
	if (xw >= w) xw = w - 1;
	for (int y = 0; y < 24; y++) {
		fb[y * w + xw] = 0x92;               /* grey: expected */
		fb[(y + 26) * w + xp] = 0xFF;        /* white: actual  */
	}
	fb_present();
}

int main(void)
{
	sys_init();
	sys_diag(0xBEAC0001);

	for (int i = 0; i < N; i++)
		wave[i] = ((i / 109) & 1) ? -12000 : 12000;   /* ~220 Hz square */

	vmx_sample_t s_once = { .data = wave, .frames = N, .format = VMX_FMT_S16,
	                        .loop = VMX_LOOP_NONE, .loop_start = 0, .loop_end = 0 };
	vmx_sample_t s_loop = { .data = wave, .frames = N, .format = VMX_FMT_S16,
	                        .loop = VMX_LOOP_FWD, .loop_start = 0, .loop_end = N };
	vmx_sample_flush(&s_once);

	uint32_t phase440 = 0;
	for (;;) {
		/* --- BLUE: CPU stream control --- */
		fill_screen(0x03);
		uint64_t t0 = sys_ticks_us64();
		while (sys_ticks_us64() - t0 < 2000000) {
			int16_t buf[2 * 800];
			for (int i = 0; i < 800; i++) {
				int16_t v = ((phase440 / 54) & 1) ? -12000 : 12000;
				phase440++;
				buf[2 * i] = buf[2 * i + 1] = v;
			}
			audio_stream_write(buf, 800);
		}

		/* --- MAGENTA: one-shot, keyed exactly once --- */
		int v1 = vmx_key_on(-1, &s_once, 1u << 16, 255, 128);
		sys_diag(0xBEAC0200u | (uint32_t)(v1 & 0xFF));
		t0 = sys_ticks_us64();
		while (sys_ticks_us64() - t0 < 2000000) {
			uint64_t el = sys_ticks_us64() - t0;
			uint32_t want = el < 1000000 ? (uint32_t)(el * 48 / 1000) : N - 1;
			pos_frame(0xE3, v1 >= 0 ? vmx_pos(v1) : 0, want);
		}
		if (v1 >= 0) vmx_key_off(v1, 0);

		/* --- GREEN: hardware loop --- */
		int v2 = vmx_key_on(-1, &s_loop, 1u << 16, 255, 128);
		sys_diag(0xBEAC0300u | (uint32_t)(v2 & 0xFF));
		t0 = sys_ticks_us64();
		while (sys_ticks_us64() - t0 < 2000000) {
			uint64_t el = sys_ticks_us64() - t0;
			uint32_t want = (uint32_t)((el * 48 / 1000) % N);
			pos_frame(0x1C, v2 >= 0 ? vmx_pos(v2) : 0, want);
		}
		if (v2 >= 0) vmx_key_off(v2, 0);
	}
}
