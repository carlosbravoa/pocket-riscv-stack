# Audio Voice Mixer — capability spec (scope & contract)

**Status:** PROPOSAL / requirements. Not yet built.
**Author of record:** raised while porting OpenJazz (Jazz Jackrabbit), whose
music is PCM-sample tracker music the CPU cannot mix in real time at 74 MHz.
**Deliberately excludes RTL/implementation** — this is *what the platform needs
the block to do and how software drives it*, so a hardware designer can size and
build it. Numbers marked *(negotiable)* are targets to trade against FPGA cost.

---

## 1. Why this exists

The SoC today has exactly two audio blocks, and neither can play sample-based
music:

- **OPL3 FM** (`opl_write`, FM flavor only) — synthesizes FM tones. Useless for
  digitized/sample music.
- **A 48 kHz stereo output FIFO** (`audio_stream_write`, ~2048-frame / ~42 ms
  depth) — a dumb sink; it plays back whatever *already-mixed* stereo the CPU
  hands it.

So **all** sample mixing is done by the CPU. `pcm_play()`'s 4 voices are
"software-mixed in the HAL" (its own comment), and any tracker/mod music has to
resample-and-sum every one of 48,000 stereo samples/second across all channels
on an rv32im core with no FPU. That is the wall OpenJazz hit — its Epic MASI
(`.PSM`) music is PCM instruments, and the software mixer (soft-float) never
returns in time, so the game never presents its first frame.

The missing piece is a **hardware PCM sample voice mixer** — the audio analog of
the 2D blitter already in this SoC. It is exactly what the Gravis UltraSound did
(the card Jazz Jackrabbit was written for): N voices, each DMA-streaming a sample
from memory at a variable pitch with volume and pan, summed in hardware. With it,
a music player becomes *a few register writes per note* and **zero CPU cost per
sample**.

This is a platform capability, not a one-game fix: it becomes the console's
general "sample playback engine."

---

## 2. The capability in one sentence

A fixed bank of hardware **voices** that each continuously read a PCM sample from
DRAM, resample it to a programmable pitch, apply volume + stereo pan, and sum all
active voices into the existing 48 kHz stereo output — driven entirely by a small
per-voice register interface the CPU updates on music/effect events.

---

## 3. Programming model (the abstraction software depends on)

The unit exposes **V voices**. Each voice is an independent state machine the CPU
configures and triggers. A "note" in a tracker, or a one-shot sound effect, is
one voice programmed and gated on.

### Per-voice state (what software must be able to set)

| Field | Meaning | Why it's required |
|---|---|---|
| **sample base** | byte address of the sample in DRAM | instruments/SFX live in DRAM (loaded from the pak); the player points a voice at one |
| **sample length** | number of samples | end of a one-shot; loop bounds reference it |
| **format** | 8-bit signed, 8-bit unsigned, 16-bit signed | trackers use all three; MASI is 8-bit PCM, many mods 8/16-bit |
| **pitch (phase step)** | fractional playback-rate increment (e.g. ≥16.16) | one voice must play one sample at *any* pitch — this IS the resampling |
| **loop mode** | none / forward / ping-pong | sustained instruments loop; ping-pong used by some formats |
| **loop start / loop end** | sample indices | loop region for sustained notes |
| **volume** | per-voice level | note velocity + tracker volume column |
| **pan** | stereo position (at least L/R balance) | tracker pan; stereo mixing |
| **gate / trigger** | key-on (start/retrigger), key-off (stop or run-to-loop-end) | note on/off |

### Per-voice readback (what software must be able to poll)

| Field | Why |
|---|---|
| **active/finished** | one-shot SFX: know when a voice is free; tracker: note-end |
| **current position** | some tracker effects read/seek position; also lets the player reclaim voices |

### Global

- **Master volume** (optional but wanted) — cheap fade-out/duck without touching
  every voice.
- **Saturating mix** — the hardware sum must clip, not wrap, on overload.

That is the whole contract software cares about. Everything else (how the DMA
fetches, how interpolation is computed, buffering) is implementation.

---

## 4. Functional requirements

**R1 — Voice count: design for 32.** Recommended **32 voices**; accept **24** if
DRAM bandwidth/FPGA cost forces it; **16 is the hard floor**, never below.

Reasoning (grounded in real content):
- **32 is the GUS number.** The Gravis UltraSound — the card Jazz Jackrabbit and
  the whole Epic sample-music catalog were authored *for* — did up to 32 voices.
  Era content was written against a ≤32-voice ceiling, so 32 reproduces it with
  nothing stolen. It also covers S3M (32 native), plays XM/IT (up to 64, but
  almost never 64 ringing at once — they voice-steal gracefully), and leaves
  headroom for the driving case: JJ1's **16 music channels** (its MASI player
  reads the count per song, default/max 16) **+ ~8 simultaneous SFX** + slack.
  It's also the prize enabler for soundfont/GUS-patch MIDI synth (Doom-class),
  which is voice-hungry. And it's a clean power of two for voice indexing.
- **24** still plays all of JJ1 and most trackers; awkward non-power-of-2 and
  minor headroom loss. Acceptable compromise.
- **16** exactly covers JJ1's music but leaves *zero* room for SFX-over-music
  without stealing a music voice, and S3M/XM start dropping notes audibly.
  Minimum viable, not ideal.
- **64** buys real IT/XM completeness and full GM polyphony, but roughly doubles
  DRAM read bandwidth and multiplier count for content that essentially never
  needs it. Diminishing returns.

Because the HAL owns voice **allocation and stealing**, games never hard-code the
count — a future SoC revision that moves 16→32→64 just changes how gracefully
songs steal, with **no game rebuild**. Pick 32 now; the number is not an ABI
lock-in for game binaries.

**R2 — Sample formats.** 8-bit signed **and** unsigned PCM, and 16-bit signed
PCM, all mono. (Stereo samples are rare in trackers and out of scope — see §9.)

**R3 — Arbitrary pitch / resampling.** Each voice plays its sample at any rate
via a fractional phase accumulator; sample rate of the source is irrelevant (the
player computes the step from note → target-rate ratio).

**R4 — Interpolation.** At least **linear** interpolation between sample points
for acceptable quality *(negotiable — nearest-neighbor is a fallback, but it
aliases badly on pitched-up samples; linear is the sweet spot for cost/quality)*.

**R5 — Looping.** Forward loops mandatory; ping-pong desirable *(negotiable)*.
Loop start/end programmable per voice; key-off can either stop immediately or
play to loop end (music vs. SFX behavior — expose both).

**R6 — Volume + stereo pan** per voice, updatable while the voice plays (trackers
change volume/pan every tick, ~50–125 Hz).

**R7 — Hardware summation into the existing 48 kHz stereo path.** The mixer runs
free-running off the same sample clock as today's FIFO. The CPU never feeds
samples for voice audio.

**R8 — Glitch-free updates.** A CPU register update mid-playback (new pitch,
volume, or a key-on) must not corrupt the currently-playing sample or produce a
click beyond the intended edit. However the hardware achieves it (latching,
safe-point application) is implementation; the *contract* is "updating a live
voice is safe and defined."

**R9 — Samples reside in DRAM.** The game loads sample data into DRAM (from the
pak, like every other asset) and hands the mixer byte addresses. Define alignment
and the addressable region; samples share the game's DRAM budget.

**R10 — Feature-gated + discoverable.** Presence advertised via a new
`HAL_FEAT_*` bit (like `HAL_FEAT_FM`/`HAL_FEAT_BLIT`), so a build without the
mixer degrades gracefully (games fall back to the software `pcm_play`/silence).

---

## 5. Integration with the existing stack

- **Coexists with OPL3.** FM (`opl_write`) and the voice mixer are independent
  sources; their outputs both reach the DAC. A game can use OPL music *and*
  sampled SFX, or sampled music alone. Define whether they sum in hardware or the
  final mix point — but they must not be mutually exclusive.
- **Coexists with the streaming FIFO.** `audio_stream_write` (CPU-fed 48 kHz)
  stays for genuinely pre-mixed / streamed content (digitized speech, a whole
  pre-rendered song). The voice mixer is **additive**, not a replacement — decide
  whether stream + voices sum, or the stream becomes "voice 0".
- **Absorbs `pcm_play`.** The 4 software voices become a thin wrapper over
  hardware voices; the per-frame software `audio_pump()` mix goes away for games
  that adopt the mixer. Existing `pcm_play` callers should keep working via the
  HAL with no source change.
- **ABI is append-only (LOCKED v1).** Every control/status register is a *new*
  CSR appended per `soc/abi/ABI.md`; nothing existing moves. This is a deliberate
  ABI minor-version bump, not a free add.
- **Hardware-fragile, like the FM path.** New audio RTL on this platform must be
  hardware-tested on silicon, not trusted from sim/timing alone (see the FM
  SDRAM-read history). Budget for bring-up.

---

## 6. Software / HAL interface (shape, not final API)

The HAL would grow a small voice API alongside the existing audio calls —
conceptually:

```c
// discover
bool     have_voice_mixer(void);              // sys_caps()->features & HAL_FEAT_VOICES

// one-time: describe a sample sitting in DRAM
voice_sample_t s = { .data=ptr, .frames=n, .format=VMX_S16,
                     .loop=VMX_LOOP_FWD, .loop_start=..., .loop_end=... };

// per note / per SFX
int  vmx_key_on (int voice, const voice_sample_t *s, int pitch_step,
                 int volume, int pan);        // start/retrigger
void vmx_set    (int voice, int pitch_step, int volume, int pan);  // live update
void vmx_key_off(int voice, bool run_to_loop_end);
int  vmx_active (int voice);                  // 0 = free
uint32_t vmx_pos(int voice);                  // current sample position

void vmx_master (int volume);                 // global
```

A tracker player then does: on each tick (~every 8–20 ms), for each channel with
a new event, call `vmx_key_on`/`vmx_set`. That is a handful of register writes per
tick — trivial CPU load — versus mixing 48k stereo samples/second today.

The exact register/CSR layout is for the hardware+HAL implementation; this spec
only fixes the *capabilities* those registers must expose (§3–§4).

---

## 7. Future projects this unlocks (why the scope is what it is)

This is the platform's sample engine, so scope is set by the *union* of what
plausible future ports need — not just OpenJazz:

- **OpenJazz (now):** Epic MASI `.PSM` music, ≤~16 channels of 8-bit PCM. The
  immediate driver.
- **Any MOD / S3M / XM / IT tracker port or player:** the classic demoscene/DOS
  module formats. MOD=4ch, S3M=up to 32, XM/IT=up to 64. 16-bit samples, loops,
  pan, volume — all in R1–R6. (XM/IT's 64 channels exceed a 32-voice target;
  those songs would voice-steal, acceptable.)
- **Doom / Heretic / Hexen:** digitized SFX (DMX) → voices directly; and if music
  is done as a GUS-patch / soundfont MIDI synth instead of OPL, that's *many*
  short looped samples pitched per note — exactly this unit, and a strong reason
  to favor more voices.
- **Wolf3D / Blake Stone:** digitized "Sound Blaster" SFX → voices (music stays
  OPL/IMF on the FM flavor).
- **Tyrian and other OPL-music games:** unchanged music (OPL), but their
  digitized SFX offload to voices.
- **General:** speech/voice samples, UI sounds, and any game that today would
  stress the 4 software `pcm_play` voices.

Design implications from that union:
- Favor **more voices** (32 over 16) if FPGA budget allows — MIDI-synth music and
  dense trackers are voice-hungry.
- **16-bit samples** are non-negotiable for anything past DOS-era 8-bit content.
- **Linear interpolation** matters more as ports pitch samples across wide ranges
  (soundfont synthesis spans octaves from one sample).

Explicit **future extensions** to keep in mind but NOT require now (so today's
block doesn't foreclose them):
- **Hardware ADPCM/IMA decode** per voice — trackers/soundfonts are large; halving
  DRAM footprint and bandwidth is attractive later.
- **Per-voice volume/pan ramping** in hardware — removes zipper noise without the
  CPU updating every tick; nice-to-have.
- **A sample-clock counter / sync tick** the player can pace off precisely, and/or
  an optional "voices drained" signal — helps tight music timing.
- **A streaming voice** (auto-refilled ring from DRAM) for long pre-rendered audio
  — though the existing FIFO already covers that.

---

## 8. Non-goals / explicitly out of scope

- **Not** a DSP/effects unit (no reverb, no hardware filters/resonance). Trackers
  that use resonant filters are rare here; the CPU can approximate if ever needed.
- **Not** stereo *source* samples (mono voices only; pan provides the stereo
  image).
- **Not** a MIDI/tracker *sequencer* in hardware — sequencing, envelopes, effects
  columns, and timing stay in the CPU player. The hardware only plays voices.
- **Not** a replacement for OPL3 — FM music keeps its dedicated path.
- **Not** compressed-audio (Ogg/MP3) decode — out of scope entirely.

---

## 9. Open questions for the hardware/ABI owner

1. **Voice count** — spec recommends **32** (the GUS ceiling; see R1). Fall back
   to **24** only if DRAM bandwidth/logic forces it; **never below 16**. Drives
   FPGA multipliers + DRAM read bandwidth, so it may be capped by bandwidth (Q4)
   rather than logic. Not an ABI lock-in — the HAL's voice-stealing means game
   binaries survive a later change to the count.
2. **Interpolation** — linear, or nearest to save resources? (Software strongly
   prefers linear.)
3. **Mix point** — do voices sum with the OPL3 output and the streaming FIFO in
   hardware, or is there a defined final-mix stage? Affects the FM-flavor build.
4. **DRAM bandwidth** — 32 voices × up to ~48 kHz × up to 2 bytes, plus scanout,
   blitter, and CPU on the same crossbar. Is there headroom, or does voice count
   get capped by bandwidth rather than logic?
5. **Both flavors or FM-only?** OPL3 is FM-flavor-only today. Should the voice
   mixer ship on *base* too (so sample music works without the FM build), or ride
   with FM? (Software preference: base too — sample music shouldn't require the
   FM bitstream.)
6. **Update model** — is a plain "write register, takes effect next sample" safe
   enough (R8), or is a latch/commit needed for multi-field atomic note starts?

---

## 10. Minimal acceptance (how we'd know it's right)

- A CPU test drives, say, 8 voices with different pitches/loops/pans from DRAM
  samples and the DAC output matches a reference software mix (within
  interpolation tolerance) — verifiable in the full-system sim + on hardware.
- The OpenJazz MASI player, rewritten to poke voices instead of soft-mixing,
  plays its music with **negligible CPU** (freeing the frame budget the software
  mixer consumed) and boots to gameplay with music on.
