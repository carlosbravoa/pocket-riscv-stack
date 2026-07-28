/* vmxprobe — audible hardware A/B of the two PCM paths, for the FM-core
 * SFX-silence investigation. Loops forever:
 *
 *   BLUE screen,   2 s:  440 Hz square through audio_stream_write()
 *                        (CPU pushes every sample; no DRAM DMA involved)
 *   MAGENTA screen, 2 s: 440 Hz square through a vmx voice
 *                        (samples fetched from DRAM by the mixer's DMA)
 *
 * Both paths audible  -> vmx works on this core.
 * Blue beeps, magenta silent -> the vmx DMA/mix is dead on this bitstream
 * (RTL sim already proves both paths identical, so that points at silicon:
 * the FM flavor's marginal SDRAM read + the vmx prefetch).
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <stdint.h>
#include "hal.h"

#define TONE_HZ   440
#define FRAMES    (48000 / 60)

static int16_t vmx_wave[480];  /* one 100 Hz-period buffer of 440 Hz square */

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

int main(void)
{
	sys_init();
	sys_diag(0xBEAC0001);

	/* rgb332-ish defaults: 0x03 blue, 0xE3 magenta */
	for (int i = 0; i < 480; i++)
		vmx_wave[i] = ((i * TONE_HZ / 48000) & 1) ? -12000 : 12000;

	uint32_t phase440 = 0;
	for (;;) {
		/* --- CPU stream phase: BLUE --- */
		fill_screen(0x03);
		uint64_t t0 = sys_ticks_us64();
		while (sys_ticks_us64() - t0 < 2000000) {
			int16_t buf[2 * FRAMES];
			for (int i = 0; i < FRAMES; i++) {
				int16_t v = ((phase440 / 54) & 1) ? -12000 : 12000;
				phase440++;           /* 48000/440/2 ~ 54 */
				buf[2 * i] = buf[2 * i + 1] = v;
			}
			audio_stream_write(buf, FRAMES);
		}

		/* --- vmx voice phase: MAGENTA --- */
		fill_screen(0xE3);
		int ch = pcm_play(0, vmx_wave, 480, 48000);
		sys_diag(0xBEAC0100u | (uint32_t)(ch & 0xFF));
		t0 = sys_ticks_us64();
		uint64_t kick = t0;
		while (sys_ticks_us64() - t0 < 2000000) {
			audio_pump();
			/* one-shot 10 ms buffer: re-kick just before it runs out */
			uint64_t now = sys_ticks_us64();
			if (now - kick > 9000) {
				pcm_play(0, vmx_wave, 480, 48000);
				kick = now;
			}
		}
	}
}
