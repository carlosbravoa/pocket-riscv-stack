# RISC-V Stack v1.1.0 — the console learns to sing

One headline feature: a **32-voice hardware PCM sample mixer** — the audio
equivalent of the 2D blitter, and the missing half of the console's sound story.

## The voice mixer (ABI 1.1, both flavors)

Until now the console had FM synthesis (OPL3, FM flavor) and a CPU-fed stereo
stream — and every digitized sound had to be mixed by the CPU, sample by
sample, on a 74 MHz rv32im with no FPU. Tracker music was simply impossible.

v1.1.0 adds a bank of **32 hardware voices**. Each one DMA-streams a PCM sample
(8-bit signed/unsigned or 16-bit) straight from DRAM, resamples it to any pitch
with linear interpolation, applies volume and stereo pan, and the hardware sums
them all into the 48 kHz output. Playing a note costs a few register writes;
playing it for a second costs the CPU **nothing**.

- Loops (forward, with loop points) for sustained instruments; one-shots
  self-retire; atomic, click-free note starts; master volume for free fades.
- Present on **both** flavors — sample music does not require the FM core.
- `HAL_FEAT_VOICES` feature bit; graceful fallback on older bitstreams.
- The classic GUS number: 32 voices covers S3M natively, voice-steals XM/IT
  gracefully, and leaves SFX headroom over any tracker song.

### What it enables

- **Existing games, free:** anything using the HAL's `pcm_play()` sound
  effects now plays them in hardware with zero source changes.
- **Tyrian** (updated `tyrian.bin` included): all 8 SFX channels moved to
  hardware voices — the per-sample software mixing is gone from the frame
  budget, and SFX gain hardware interpolation on the way.
- **Coming:** tracker/MOD players, GUS-patch MIDI synthesis, and the sample-
  music games (Jazz Jackrabbit-class) this block was designed for.

### SDK

New `vmx_*` HAL API (`soc/hal/hal.h`): `vmx_key_on/set/key_off`, position and
active-mask readback, master volume — implemented for hardware, the PC twin
(desktop-first development), and as a documented no-op fallback. See the
"Sample music the real way" section in `sdk/GUIDE.md`.

## Also in this release

- **The 71-minute freeze is dead.** A long session could freeze the game with
  music still playing: the millisecond clock derived from a 32-bit microsecond
  counter wrapped at a non-power-of-two boundary, breaking every game's frame
  pacing. The HAL now keeps 64-bit time and `SDL_GetTicks()` honors the real
  SDL contract (wraps at 49.7 days).
- **Engineering under the hood** (see `soc/REPRODUCIBILITY.md`): every build
  is byte-reproducible; the SDRAM interface is timing-constrained and every
  compile is gated on the hardware-validated capture window plus full-design
  timing closure; the released bitstreams are byte-identical to the exact
  cores that were hardware-verified. The v1.1 CSR additions are pure appends —
  every existing game binary keeps running.

## Install

Unzip `RiscvStackFamily_v1.1.0.zip` at the root of the Pocket SD card (replaces
any previous version; saves are kept). Games (`.bin`) and data paks go in
`Assets/riscv_stack/common/`.

---
Verified in bit-exact simulation, full-system RTL simulation with audio-output
assertions, and on hardware before release.
