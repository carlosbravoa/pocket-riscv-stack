# Audio Voice Mixer — engineering scoping (companion to AUDIO_VOICE_MIXER_SPEC.md)

**Status:** SCOPING — sizes the spec against this SoC and answers its §9 open
questions. Spec = *what*; this = *how big, where, in what order*.
Grounded in the SoC as of v1.0.0 (74.25 MHz sys, 16-bit SDR DRAM, LiteDRAM
crossbar, base fit 39 % ALMs / FM 59 %).

---

## 1. Feasibility numbers (the go/no-go math)

### DRAM bandwidth — NOT the limiter (§9 Q4: headroom is ample)
- Bus peak: 16 bit × 74.25 MHz ≈ **148 MB/s**.
- Existing steady load: scanout 320×240×8bpp×60 ≈ **4.6 MB/s** + CPU + blitter
  bursts (episodic).
- Mixer raw data, worst case: 32 voices × 48 kHz × 2 B = **3.1 MB/s ≈ 2 %**.
- The real cost is access *pattern*, not bytes: naïve per-sample fetches would be
  1.5 M scattered single-beat reads/s (~15–20 % of DRAM time in row overhead).
  **Design requirement: per-voice prefetch FIFOs** (8–16 samples, burst-filled,
  round-robin). That amortizes to ~200–400 k bursts/s ≈ **3–5 % of DRAM time**.
- Conclusion: **32 voices clears easily**; voice count is capped by neither
  bandwidth nor compute (below). Take the spec's 32.

### Compute — one time-multiplexed MAC lane
- 74.25 MHz / 48 kHz = **1 547 sys cycles per output frame**.
- Per voice per frame: linear interp (1 mult) + volume (1) + pan L/R (2) ≈ 4
  mult-ops → 32 voices ≈ **128 mult slots vs 1 547 available** — one pipelined
  DSP lane at <10 % duty, OPL3-operator style iteration over voice state.
- Linear interpolation costs one extra mult in a budget with 10× headroom —
  **take it** (§9 Q2: linear, yes).

### FPGA budget (estimate, to be confirmed by the first fit)
| resource | mixer est. | base after | FM after |
|---|---|---|---|
| ALMs | ~2–3 k (DMA + voice FSM + MAC pipeline + CSR) | ~39→~53 % | ~59→~73 % |
| M10K | 2–3 (voice state; prefetch FIFOs 32×16×16b; output) | 61 %→+1 % | 61 %→+1 % |
| DSP  | 1–2 | 6 %→~9 % | 8 %→~11 % |

FM is the tight flavor but fits. The determinism pins + SDRAM window gate mean
the added pressure cannot silently break the DRAM path — a bad fit now fails the
build.

## 2. Architecture decisions (answers to spec §9)

1. **Voice count: 32.** Both the bandwidth and compute math close with margin;
   no reason to take the 24/16 fallbacks.
2. **Interpolation: linear.** One mult; the aliasing win is large for pitched-up
   soundfont/tracker content.
3. **Mix point: in the SoC, before core_top.** Voices sum (saturating) with the
   existing CPU stream in the sys-side audio path, exactly where
   `audio_stream` lives today; the summed SoC audio then feeds core_top as now,
   and the FM flavor's OPL3 adds at its existing core_top mix. This keeps the
   SoC **identical between flavors** (family-ABI premise intact) and makes Q5
   automatic:
4. **Both flavors get the mixer.** Sample music must not require the FM
   bitstream; base has the most headroom anyway.
5. **DMA: one new `LiteDRAMDMAReader` on the crossbar** — the proven in-tree
   pattern (blitter reader/writer, pak DMA writer). Round-robin service of the
   32 prefetch FIFOs; scanout keeps its existing arbitration position (verify
   underrun margin in sim with blitter saturating the bus — acceptance test).
6. **Update model (Q6): shadow + commit.** Per-voice config fields stage into
   shadow registers behind a voice-select index; a commit strobe applies them
   atomically at the voice's next sample tick (glitch-free key-on, R8). Plain
   live writes allowed for vol/pan/step (single-field, click-free by latching at
   sample boundary).

## 3. CSR / ABI shape (append-only, ABI v1 → v1.1)

Indexed window keeps the appended-CSR count small (~12) instead of 32×9 flat:

```
vmx_sel        W   voice index (0..31)
vmx_base       W   sample base byte address (DRAM offset)
vmx_len        W   frames
vmx_fmt        W   format (s8/u8/s16) | loop mode (none/fwd/pingpong)
vmx_loop_start W   frames
vmx_loop_end   W   frames
vmx_step       W   16.16 phase increment
vmx_volpan     W   vol[15:8] pan[7:0]
vmx_ctrl       W   strobe: bit0 commit+key_on, bit1 key_off, bit2 key_off_to_loop_end
vmx_pos        R   selected voice: current frame position
vmx_active     R   32-bit bitmask, ALL voices' active flags in one read
vmx_master     W   master volume
```

- All **appended** per `soc/abi/ABI.md`; `check_abi.py` enforces. Minor bump:
  `ABI_VERSION 1.0 → 1.1` (both branches, same values — family ABI).
- New feature bit `HAL_FEAT_VOICES` in `hwfeat` (R10); base and FM both set it.
- `vmx_active` as a bitmask means voice allocation costs one CSR read.

## 4. HAL & software plan

- New `vmx_*` API as sketched in the spec §6; implemented three times:
  1. **hardware** (CSR pokes),
  2. **PC twin** (software mixer — games developable before/without RTL),
  3. **fallback** when `HAL_FEAT_VOICES` absent: `vmx_key_on` degrades to the
     existing 4-voice software `pcm_play` path (best-effort, documented).
- `pcm_play()` internally becomes `vmx_*` when the feature bit is set — existing
  games get hardware mixing with **zero source change** (spec §5).
- `audio_pump()` drops its per-frame software mix for adopting games.

## 5. Verification plan (uses everything we just built)

1. **Bit-exact unit bench** (the `fm_resample` discipline): Verilog voice-pipeline
   bench vs a numpy reference mixer — same stimulus, diff == 0 (within the
   documented interp rounding). Runs in CI-style via `run_sim.sh` harness.
2. **Full-system sim scenario**: 8 voices, different pitches/loops/pans, from
   DRAM samples, DAC stream captured and compared to the reference mix
   (spec §10 acceptance) — plus a stress variant with the blitter hammering the
   bus to prove scanout + voices never underrun.
3. **PC twin parity test**: the same test program produces the same audio on the
   twin.
4. **Hardware bring-up**: deterministic builds + the SDRAM window gate already
   protect the risky shared path; the mixer itself is sys-domain logic (low
   analog risk — the audio CDC reuses the existing stream FIFO crossing).
   One flash per flavor with the §10 acceptance program.

## 6. Phasing (each phase lands green before the next)

| phase | deliverable | est. effort |
|---|---|---|
| P0 | this scoping reviewed; CSR names frozen | **DONE 2026-07-26** |
| P1 | RTL: voice core + unit bench bit-exact (soc/voice_mixer/, PASS 0 mismatches) | **DONE 2026-07-26** |
| P2 | SoC integration (vmx CSR region, line-cache DMA, hw mix, ABI 1.1) + in-system vmxtest acceptance (PASS on FM sim) | **DONE 2026-07-26** |
| P3 | HAL `vmx_*` (hw + PC twin + fallback), `pcm_play` rebased onto it | 1 session |
| P4 | hardware bring-up both flavors (one flash each) | ½ session + Carlos |
| P5 | OpenJazz MASI player on voices — the driving acceptance (spec §10) | 1 session |

## 7. Risks & watch-items

- **FM fit pressure** (59→~73 % ALMs): the gate protects the SDRAM window, but
  overall Fmax closure may need effort; keep the MAC pipeline shallow and let
  the fitter retime. If FM cannot close, ship base-first (feature bit makes
  that graceful) — but expect it to close.
- **Scanout underrun under combined DMA load**: covered by the stress sim before
  any hardware.
- **Zipper noise** on vol/pan steps: mitigated by sample-boundary latching; the
  spec's ramping extension stays future work.
- **ABI discipline**: the 12 CSRs are an append; run `check_abi.py` in both
  branches before the first commit that elaborates them.
