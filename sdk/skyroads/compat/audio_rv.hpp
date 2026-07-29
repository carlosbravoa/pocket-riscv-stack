// Skyroads-c on riscv-stack: the console audio driver.
//
// Replaces upstream's AudioMixer (audio.cpp), which renders music + SFX into
// one 48 kHz software stream — a per-sample float pipeline this no-FPU CPU
// cannot afford and the hardware makes unnecessary:
//
//   music  MuzaxPlayer sequencer kept, advanced by TIME (180.02 Hz ticks from
//          sys_ticks_us64); its OPL register writes reach the real OPL3 via
//          compat/opl_chip_hw.cpp. Silent on non-FM flavors (house policy).
//   SFX    8-bit PCM converted to int16 once at init, played fire-and-forget
//          through pcm_play() — which the v1.1 HAL routes onto hardware
//          voice-mixer voices 28..31 when the bitstream has one.
//
// Reuses upstream's OplSynth (the faithful AdLib driver reproduction) and
// MuzaxPlayer untouched, plus one RVSTACK: addition: MuzaxPlayer::advance(),
// the time-driven twin of render().
#pragma once

#include <cstdint>
#include <vector>

#include "audio/audio.hpp"

namespace rvstack {

class AudioRv {
public:
	explicit AudioRv(skyroads::audio::AttractAudioAssets assets);
	void apply_commands(const std::vector<skyroads::core::AudioCommand>& commands);
	// Diagnostic access to a converted SFX buffer (SKY_AUDIO_SELFTEST).
	const std::vector<int16_t>& sfx_buffer(std::size_t i) const { return sfx_i16_[i]; }
	// Call once per host loop iteration; drives the music sequencer.
	void advance(uint64_t now_us);

private:
	skyroads::audio::AttractAudioAssets assets_;
	skyroads::audio::OplSynth synth_;
	skyroads::audio::MuzaxPlayer player_;
	// SFX pre-converted to the int16 pcm_play() wants, upstream gains baked in
	// (0.55 for SFX, INTRO_GAIN for the intro sample).
	std::vector<std::vector<int16_t>> sfx_i16_;
	std::vector<int16_t> intro_i16_;
	uint64_t last_us_ = 0;
	uint64_t last_sfx_us_ = 0;  // the DOS bounce re-trigger gate (222 ms)
};

} // namespace rvstack
