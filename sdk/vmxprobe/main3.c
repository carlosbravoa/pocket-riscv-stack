/* vmxprobe3 — NUMERIC hardware check of the mixer's interpolator inputs.
 *
 * The 2026-07-28 defect (fractional steps play noise, step 1.0 is perfect)
 * could be a bad FETCH or a bad MULTIPLY, and audio alone cannot tell them
 * apart. This uses the vmx_dbg_pair CSR: the mixer reports the two samples it
 * last handed its interpolator, so software can compare them against the
 * sample data it actually loaded.
 *
 * A voice plays a RAMP (sample[i] = i*8) so every index has a unique value and
 * any stale/swapped/garbled fetch is obvious in the number itself.
 *
 * Screen: background = which step is running (green 1.0, yellow fractional).
 * TOP RED bar    = fetched pair was wrong (width = count).
 * BOTTOM BLUE bar= the mixer's interpolated output disagrees with the same
 *                  arithmetic redone in software from ITS OWN reported
 *                  s0/s1/frac — i.e. the multiply is the broken term.
 * Two markers in between = the pair's indices (white s0, cyan s1). Diag words carry the detail:
 *   0xD060xxxx  pass count      0xD0B0xxxx  bad-fetch count
 *   0xD017xxxx  bad-interpolation count
 *   0xD051xxxx  last s0 index   0xD052xxxx  last s1 index (0xFFFF = unknown)
 *
 * Phases (2 s each, forever): step 1.0 (known good) then step 1/6 (the broken
 * one). If step 1/6 shows correct PAIRS, the fetch is fine and the multiply is
 * the culprit; if the pairs are wrong, the fetch is.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <stdint.h>
#include "hal.h"

#define N 4096

static int16_t ramp[N];

static int idx_of(int16_t v)          /* ramp value -> index, or -1 */
{
	int i = (int)v / 8;
	if (i < 0 || i >= N || ramp[i] != v) return -1;
	return i;
}

static void bars(uint8_t bg, int ok, int bad, int bad_interp, int s0i, int s1i)
{
	uint8_t *fb = fb_backbuffer();
	int w = fb_width(), h = fb_height();
	for (int k = 0; k < w * h; k++)
		fb[k] = bg;
	/* row 1: mismatch count (red), row 2: s0 index, row 3: s1 index */
	int wb = bad > w ? w : bad;
	for (int y = 4; y < 16; y++)
		for (int x = 0; x < wb; x++)
			fb[y * w + x] = 0xE0;                 /* red: bad FETCH pair */
	int wi = bad_interp > w ? w : bad_interp;
	for (int y = 52; y < 64; y++)
		for (int x = 0; x < wi; x++)
			fb[y * w + x] = 0x03;                 /* blue: bad INTERPOLATION */
	int x0 = s0i < 0 ? 0 : (int)((long)s0i * w / N);
	int x1 = s1i < 0 ? 0 : (int)((long)s1i * w / N);
	for (int y = 20; y < 32; y++) fb[y * w + (x0 < w ? x0 : w - 1)] = 0xFF;
	for (int y = 36; y < 48; y++) fb[y * w + (x1 < w ? x1 : w - 1)] = 0x1F;
	(void)ok;
	fb_present();
}

int main(void)
{
	sys_init();
	sys_diag(0xBEAC0001);

	for (int i = 0; i < N; i++)
		ramp[i] = (int16_t)(i * 8);        /* unique, and s16-safe */

	vmx_sample_t s = { .data = ramp, .frames = N, .format = VMX_FMT_S16,
	                   .loop = VMX_LOOP_FWD, .loop_start = 0, .loop_end = N };
	vmx_sample_flush(&s);

	int phase = 0;
	for (;;) {
		uint32_t step = phase ? ((8000u << 16) / 48000u) : (1u << 16);
		int v = vmx_key_on(-1, &s, step, 255, 128);
		uint32_t ok = 0, bad = 0, bad_interp = 0;
		int last0 = -1, last1 = -1;
		uint64_t t0 = sys_ticks_us64();
		while (sys_ticks_us64() - t0 < 2000000) {
			uint32_t pair = vmx_dbg_pair(v);
			uint32_t itp  = vmx_dbg_interp(v);
			int16_t s0 = (int16_t)(pair & 0xFFFF);
			int16_t s1 = (int16_t)(pair >> 16);
			uint32_t frac = itp >> 16;
			int16_t out = (int16_t)(itp & 0xFFFF);
			/* redo the mixer's own arithmetic and compare */
			int32_t want = (int32_t)s0 +
			               ((((int32_t)s1 - (int32_t)s0) * (int32_t)frac) >> 16);
			if ((int16_t)want != out)
				bad_interp++;
			int i0 = idx_of(s0), i1 = idx_of(s1);
			last0 = i0; last1 = i1;
			/* Two REAL ramp values, adjacent and in order. The LOOP WRAP
			 * is legitimate too: at the last frame the pair is
			 * (N-1, loop_start) — counting that as a mismatch is what
			 * put ~4 false reds on the first hardware run. */
			if (i0 >= 0 && i1 >= 0 &&
			    (i1 == i0 + 1 || (i0 == N - 1 && (i1 == 0 || i1 == N - 1))))
				ok++;
			else
				bad++;
			bars(phase ? 0xFC : 0x1C, (int)ok, (int)bad, (int)bad_interp, i0, i1);
		}
		if (v >= 0) vmx_key_off(v, 0);
		sys_diag(0xD0600000u | (ok  & 0xFFFF));
		sys_diag(0xD0B00000u | (bad & 0xFFFF));
		sys_diag(0xD0170000u | (bad_interp & 0xFFFF));
		sys_diag(0xD0510000u | (uint32_t)(last0 & 0xFFFF));
		sys_diag(0xD0520000u | (uint32_t)(last1 & 0xFFFF));
		phase ^= 1;
	}
}
