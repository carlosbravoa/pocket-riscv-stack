// fbbench — cycle-exact present-path autopsy for the full-system sim.
// Reports microsecond costs via diag codes: 0xFB0S_TTTT (S=stage, T=µs/16).
// Stages: 1=64KB memcpy  2=dcache flush 76800B  3=fb_present (incl wait)
//         4=full present_indexed-equivalent  5=10-present average
//
// SPDX-License-Identifier: BSD-2-Clause
#include "hal.h"
#include <string.h>
#include <system.h>              /* flush_cpu_dcache_range */
#include <generated/mem.h>

#define D(stage, us) sys_diag(0xFB000000u | ((stage) << 16) | (((us) / 16) & 0xFFFF))

static uint8_t src_frame[320 * 200];

int main(void)
{
	sys_init();
	sys_diag(0xFB000001);

	memset(src_frame, 0x5A, sizeof src_frame);
	uint8_t *fb = fb_backbuffer();

	// 1: raw 64 KB copy into the framebuffer
	uint32_t t0 = sys_ticks_us();
	memcpy(fb + 20 * 320, src_frame, sizeof src_frame);
	D(1, sys_ticks_us() - t0);

	// 2: the flush alone (what fb_present does internally, isolated)
	t0 = sys_ticks_us();
	flush_cpu_dcache_range((void *)fb, 320 * 240);
	D(2, sys_ticks_us() - t0);

	// 3: fb_present (flush + wait-for-DMA-wrap + flip)
	t0 = sys_ticks_us();
	fb_present();
	D(3, sys_ticks_us() - t0);

	// 4: a full port-style present: copy + present
	fb = fb_backbuffer();
	t0 = sys_ticks_us();
	memcpy(fb + 20 * 320, src_frame, sizeof src_frame);
	fb_present();
	D(4, sys_ticks_us() - t0);

	// 5: sustained — 10 presents, average
	t0 = sys_ticks_us();
	for (int i = 0; i < 10; i++) {
		fb = fb_backbuffer();
		memcpy(fb + 20 * 320, src_frame, sizeof src_frame);
		fb_present();
	}
	D(5, (sys_ticks_us() - t0) / 10);

	sys_diag(0xFB0000F0);
	for (;;)
		;
}
