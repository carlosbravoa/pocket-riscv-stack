/* sfxtest — the SkyRoads bounce through the REAL game path, on hardware.
 *
 * Differential: the same sample bytes, two ways, alternating forever.
 *   BLUE   2 s: pcm_play(rate=8000)  -> vmx fractional step 0x2AAA (the game)
 *   GREEN  2 s: CPU-resampled to 48 kHz, pcm_play(rate=48000) -> step 1.0
 *   (each phase plays the bounce 3 times, ~0.6 s apart)
 *
 * BLUE bad + GREEN good  -> the vmx fractional interpolation is wrong on
 *                           silicon (sim renders it at 0.99 correlation).
 * both bad               -> the fetch/DMA path with real game data.
 * both good              -> the primitive path is fine; the fault is in how
 *                           the GAME drives it (overlap, re-key, contention).
 *
 * The sim scenario still works: beacons 1/4/7 as before.
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <stdint.h>
#include "hal.h"
#include "bounce_sfx.h"

#define NS   ((int)(sizeof bounce_u8))
#define NS48 (NS * 6)          /* 8000 -> 48000 */

static int16_t pcm8[NS];       /* native 8 kHz, int16 (what the game hands over) */
static int16_t pcm48[NS48];    /* the same sound pre-resampled by the CPU */

static void fill(uint8_t c)
{
	for (int i = 0; i < 2; i++) {
		uint8_t *fb = fb_backbuffer();
		int n = fb_width() * fb_height();
		for (int k = 0; k < n; k++)
			fb[k] = c;
		fb_present();
	}
}

int main(void)
{
	sys_init();
	sys_diag(0xBEAC0001);

	/* audio_rv.cpp's conversion in fixed point: ((b/255)*2-1) * 0.96 */
	for (int i = 0; i < NS; i++)
		pcm8[i] = (int16_t)((((int32_t)bounce_u8[i] * 2 - 255) * 31456) / 255);

	/* linear interpolation to 48 kHz — the same math the vmx does in RTL */
	for (int i = 0; i < NS48; i++) {
		int src = i / 6, fr = i % 6;
		int s0 = pcm8[src];
		int s1 = pcm8[src + 1 < NS ? src + 1 : NS - 1];
		pcm48[i] = (int16_t)(s0 + ((s1 - s0) * fr) / 6);
	}
	sys_diag(0xBEAC0004);

	int phase = 0;
	for (;;) {
		fill(phase ? 0x1C : 0x03);            /* green : blue */
		for (int rep = 0; rep < 3; rep++) {
			if (phase)
				pcm_play(0, pcm48, NS48, 48000);
			else
				pcm_play(0, pcm8, NS, 8000);
			uint64_t t = sys_ticks_us64();
			while (sys_ticks_us64() - t < 700000)
				audio_pump();
		}
		if (!phase)
			sys_diag(0xBEAC0007);
		phase ^= 1;
	}
}
