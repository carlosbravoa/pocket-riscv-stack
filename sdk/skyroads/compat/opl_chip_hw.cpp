// Skyroads-c on riscv-stack: OplChip on the REAL OPL3.
//
// Console replacement for src/audio/opl_chip.cpp (+ third_party/ymfm): the
// game's OPL2 register stream goes to the FM flavor's hardware OPL3 through
// opl_write() instead of an emulated YM3812. Same class, same header —
// the pimpl means upstream code cannot tell the difference.
//
// Two deliberate divergences from the emulator build:
//  - No write queue. Upstream queues writes and drains one per chip sample to
//    model the driver's port-settle delays (@0x5876); on hardware the chip
//    clocks in real time all by itself, which is the very thing the queue was
//    simulating. opl_write()'s own pacing covers the address/data settle.
//  - next_sample() returns silence. Nothing on the console mixes the music in
//    software; the chip's output goes to the DAC in hardware. The MuzaxPlayer
//    sequencer must therefore be advanced by TIME, not by pulling samples
//    (see audio_rv.cpp / MuzaxPlayer::advance).
//
// On the PCM-only base flavor opl_write() is a no-op: music is silent, which
// is the house policy for FM music on non-FM bitstreams (Doom, Wolf3D, pop).
#include "audio/opl_chip.hpp"

extern "C" {
#include "hal.h"
}

namespace skyroads::audio {

struct OplChip::Impl {};

OplChip::OplChip(uint32_t) {}
OplChip::~OplChip() = default;
OplChip::OplChip(OplChip&&) noexcept = default;
OplChip& OplChip::operator=(OplChip&&) noexcept = default;

void OplChip::reset()
{
	// Key-off every melodic channel and the rhythm section, both the things
	// the driver's init rewrites and the ones it assumes silent.
	for (uint8_t ch = 0; ch < 9; ++ch)
		opl_write(0xB0 + ch, 0x00);
	opl_write(0xBD, 0x00);
}

void OplChip::write(uint8_t reg, uint8_t value)
{
	opl_write(reg, value);
}

float OplChip::next_sample()
{
	return 0.0f;
}

} // namespace skyroads::audio
