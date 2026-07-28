// See audio_rv.hpp.
#include "audio_rv.hpp"

extern "C" {
#include "hal.h"
}

namespace rvstack {

namespace {

// Upstream mixes ActivePcm at 0.40/0.55 gain (audio.cpp); the vmx halves a
// center-panned mono voice again (measured x2.03 on the sim DAC capture), so
// the console conversion runs ~1.75x hotter to land at the same loudness.
constexpr float INTRO_GAIN = 0.70f;
constexpr float SFX_GAIN = 0.96f;

// Convert to int16 AND resample to the hardware's native 48 kHz here, once at
// load — so every voice plays at step 1.0.
//
// WHY (hardware-proven, 2026-07-28): the console's voice mixer renders
// fractional steps incorrectly on silicon. Same sample, same buffer, same
// pcm_play call: at rate 8000 (step 0x2AAA) it comes out as noise; the
// identical audio pre-resampled to 48000 (step 1.0) is perfect — sdk/sfxtest
// is that A/B. Only the interpolation term differs (frac == 0 at step 1.0),
// and it is correct in RTL simulation (0.99 correlation vs the exact
// expected waveform), so this is a silicon-only defect awaiting an RTL fix.
// Costs ~700 KB of the 27 MB game region and one pass at startup.
std::vector<int16_t> to_i16(const skyroads::data::Pcm8Sample& s, float gain)
{
	const std::size_t n = s.samples.size();
	std::vector<int16_t> src(n);
	for (std::size_t i = 0; i < n; ++i) {
		// Same transfer curve as ActivePcm::next_sample():
		// [0,255] -> [-1,1] * gain, then the mixer's 32767 scale.
		float v = ((static_cast<float>(s.samples[i]) / 255.0f) * 2.0f - 1.0f) * gain;
		src[i] = static_cast<int16_t>(v * 32767.0f);
	}
	if (n < 2 || s.sample_rate == 0 || s.sample_rate == 48000)
		return src;

	// 16.16 walk, linear interpolation — the same math the mixer would do.
	const uint32_t step = (uint32_t)(((uint64_t)s.sample_rate << 16) / 48000u);
	const std::size_t out_n = (std::size_t)(((uint64_t)(n - 1) << 16) / step);
	std::vector<int16_t> out(out_n);
	uint32_t pos = 0;
	for (std::size_t i = 0; i < out_n; ++i, pos += step) {
		const std::size_t k = pos >> 16;
		const int32_t fr = (int32_t)(pos & 0xFFFF);
		const int32_t s0 = src[k];
		const int32_t s1 = src[k + 1 < n ? k + 1 : n - 1];
		out[i] = (int16_t)(s0 + (((s1 - s0) * fr) >> 16));
	}
	return out;
}

} // namespace

AudioRv::AudioRv(skyroads::audio::AttractAudioAssets assets)
    : assets_(std::move(assets)),
      synth_(static_cast<float>(skyroads::audio::OUTPUT_SAMPLE_RATE)),
      player_(assets_.muzax)
{
	intro_i16_ = to_i16(assets_.intro, INTRO_GAIN);
	sfx_i16_.reserve(assets_.sfx.effects.size());
	for (const auto& e : assets_.sfx.effects)
		sfx_i16_.push_back(to_i16(e.sample, SFX_GAIN));
}

void AudioRv::apply_commands(const std::vector<skyroads::core::AudioCommand>& commands)
{
	using skyroads::core::AudioCommandKind;
	for (const auto& c : commands) {
		switch (c.kind) {
		case AudioCommandKind::PlaySong:
			player_.load_song(c.value, synth_);
			break;
		case AudioCommandKind::StopSong:
			player_.stop(synth_);
			break;
		case AudioCommandKind::PlayIntroSample:
			// Everything is pre-resampled to 48 kHz (see to_i16): the
			// voices always run at step 1.0, the only path the mixer
			// gets right on silicon.
			pcm_play(0, intro_i16_.data(), (int)intro_i16_.size(), 48000);
			break;
		case AudioCommandKind::PlaySfx:
			// Polyphonic free-channel fire, exactly like the desktop
			// mixer — the DOSBox-verified ground truth. (A monophonic
			// DOS-player mode was tried and reverted with the rest of
			// the disassembly-derived audio spec.)
			if (c.value < sfx_i16_.size() && !sfx_i16_[c.value].empty()) {
				pcm_play(-1, sfx_i16_[c.value].data(),
				         (int)sfx_i16_[c.value].size(), 48000);
			}
			break;
		case AudioCommandKind::StopAllSamples:
			// pcm_play() is fire-and-forget with no stop primitive; on
			// vmx bitstreams the pcm channels live on voices 28..31, so
			// key them off directly. Software-mix flavors just let the
			// (short) samples run out.
			if (vmx_voices())
				for (int v = 28; v <= 31; ++v)
					vmx_key_off(v, 0);
			break;
		}
	}
}

void AudioRv::advance(uint64_t now_us)
{
	if (last_us_ == 0) {
		last_us_ = now_us;
		return;
	}
	uint64_t dt = now_us - last_us_;
	last_us_ = now_us;
	// A stall (level load, pak pull) must not fast-forward the song.
	if (dt > 250000)
		dt = 250000;
	player_.advance(synth_, static_cast<float>(dt) * 1e-6f);
	audio_pump();
}

} // namespace rvstack
