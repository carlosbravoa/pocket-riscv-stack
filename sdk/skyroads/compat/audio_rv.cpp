// See audio_rv.hpp.
#include "audio_rv.hpp"

extern "C" {
#include "hal.h"
}

namespace rvstack {

namespace {

// Upstream mixes ActivePcm at these gains (audio.cpp): bake them into the
// one-time int16 conversion since pcm_play() has no volume parameter.
constexpr float INTRO_GAIN = 0.40f;
constexpr float SFX_GAIN = 0.55f;

std::vector<int16_t> to_i16(const skyroads::data::Pcm8Sample& s, float gain)
{
	std::vector<int16_t> out(s.samples.size());
	for (std::size_t i = 0; i < s.samples.size(); ++i) {
		// Same transfer curve as ActivePcm::next_sample():
		// [0,255] -> [-1,1] * gain, then the mixer's 32767 scale.
		float v = ((static_cast<float>(s.samples[i]) / 255.0f) * 2.0f - 1.0f) * gain;
		out[i] = static_cast<int16_t>(v * 32767.0f);
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
			pcm_play(-1, intro_i16_.data(), (int)intro_i16_.size(),
			         (int)assets_.intro.sample_rate);
			break;
		case AudioCommandKind::PlaySfx:
			if (c.value < sfx_i16_.size() && !sfx_i16_[c.value].empty()) {
				const auto& e = assets_.sfx.effects[c.value];
				pcm_play(-1, sfx_i16_[c.value].data(),
				         (int)sfx_i16_[c.value].size(),
				         (int)e.sample.sample_rate);
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
