/* convtest — does SkyRoads' float sample conversion produce the RIGHT numbers
 * on rv32 soft-float? sfxtest (integer math) sounds correct on hardware while
 * SkyRoads (float math, same data, same 48 kHz step) sounds wrong AND quiet —
 * so the conversion itself is a suspect that no audio test can isolate.
 *
 * Runs audio_rv.cpp's to_i16() verbatim and reports the result numerically.
 * Compare against the same computation on x86 (tools print it).
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <stdint.h>
#include <stddef.h>
#include <vector>
extern "C" {
#include "hal.h"
}
#include "bounce_sfx.h"

/* --- verbatim from sdk/skyroads/compat/audio_rv.cpp ---------------------- */
static std::vector<int16_t> to_i16(const uint8_t *samples, size_t n,
                                   uint32_t sample_rate, float gain)
{
	std::vector<int16_t> src(n);
	for (size_t i = 0; i < n; ++i) {
		float v = ((static_cast<float>(samples[i]) / 255.0f) * 2.0f - 1.0f) * gain;
		src[i] = static_cast<int16_t>(v * 32767.0f);
	}
	if (n < 2 || sample_rate == 0 || sample_rate == 48000)
		return src;
	const uint32_t step = (uint32_t)(((uint64_t)sample_rate << 16) / 48000u);
	const size_t out_n = (size_t)(((uint64_t)(n - 1) << 16) / step);
	std::vector<int16_t> out(out_n);
	uint32_t pos = 0;
	for (size_t i = 0; i < out_n; ++i, pos += step) {
		const size_t k = pos >> 16;
		const int32_t fr = (int32_t)(pos & 0xFFFF);
		const int32_t s0 = src[k];
		const int32_t s1 = src[k + 1 < n ? k + 1 : n - 1];
		out[i] = (int16_t)(s0 + (((s1 - s0) * fr) >> 16));
	}
	return out;
}
/* ------------------------------------------------------------------------ */

int main(void)
{
	sys_init();
	sys_diag(0xBEAC0001);

	std::vector<int16_t> v = to_i16(bounce_u8, sizeof bounce_u8, 8000, 0.96f);

	int32_t mn = 32767, mx = -32768;
	uint32_t sum = 0;
	for (size_t i = 0; i < v.size(); ++i) {
		if (v[i] < mn) mn = v[i];
		if (v[i] > mx) mx = v[i];
		sum = sum * 31u + (uint16_t)v[i];
	}
	sys_diag(0xC0510000u | (uint32_t)(v.size() & 0xFFFF));
	sys_diag(0xC0520000u | (uint32_t)(mn & 0xFFFF));
	sys_diag(0xC0530000u | (uint32_t)(mx & 0xFFFF));
	sys_diag(0xC0540000u | (sum & 0xFFFF));
	/* first four converted samples, pre-resample sanity */
	sys_diag(0xC0600000u | (uint32_t)((uint16_t)v[0]));
	sys_diag(0xC0610000u | (uint32_t)((uint16_t)v[1]));
	sys_diag(0xC0620000u | (uint32_t)((uint16_t)v[100]));
	sys_diag(0xC0630000u | (uint32_t)((uint16_t)v[1000]));
	sys_diag(0xBEAC0007);
	for (;;)
		;
}
