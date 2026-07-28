/* sfxtest — plays the SkyRoads bounce SFX through pcm_play() exactly as the
 * port does (same u8->s16 conversion, same gain, rate 8000), so the sim can
 * capture the SoC DAC output for comparison against the desktop-truth mixer.
 * Beacons ride the RVSTACK_SKYROADS scenario: 1 boot, 4 sample started,
 * 7 sample done (+0.2 s tail) = scenario PASS/exit.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <stdint.h>
#include "hal.h"
#include "bounce_sfx.h"

static int16_t pcm[sizeof bounce_u8];

int main(void)
{
	sys_init();
	sys_diag(0xBEAC0001);

	/* audio_rv.cpp's to_i16(): ((b/255)*2-1)*0.55*32767, in fixed point */
	for (unsigned i = 0; i < sizeof bounce_u8; i++) {
		int32_t centered = (int32_t)bounce_u8[i] * 2 - 255;   /* [-255,255] */
		pcm[i] = (int16_t)((centered * 18022) / 255);         /* 0.55*32767 */
	}

	sys_diag(0xBEAC0004);
	pcm_play(0, pcm, (int)(sizeof bounce_u8), 8000);

	/* 0.64 s sample + 0.2 s tail at 36 loop-Hz via the timebase */
	uint64_t t0 = sys_ticks_us64();
	while (sys_ticks_us64() - t0 < 850000)
		audio_pump();

	sys_diag(0xBEAC0007);
	for (;;)
		;
}
