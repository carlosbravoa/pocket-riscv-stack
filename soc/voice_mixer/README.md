# Voice mixer — 32-voice hardware PCM sample engine (P1: core + bit-exact bench)

Implements `soc/AUDIO_VOICE_MIXER_SPEC.md` per `soc/AUDIO_VOICE_MIXER_SCOPING.md`.
This directory is phase P1: the mixer core and its bit-exact reference bench
(the `fm_resample` discipline). DMA/prefetch + CSR glue are P2 (SoC integration).

- `voice_mixer.v`    — the core: NV voices, sequential per-frame FSM, ONE shared
                       multiplier (1 DSP), per-voice state in inferred RAM/regs.
- `gen_ref.py`       — numpy reference mixer, generates `tb_samples.txt`,
                       `tb_cfg.txt`, `tb_gold.txt`.
- `voice_mixer_tb.v` — bench: behavioral sample RAM serves the fetch port,
                       configures voices, runs frames, compares L/R bit-exact.
- `run_bench.sh`     — gen_ref + verilator --binary + run.

## The arithmetic law (bit-exact contract, mirrored in gen_ref.py)

All integer, all floor semantics (arithmetic shift):

```
pos        : 24.16 fixed-point frame position (u24 int part, u16 frac)
idx0       = floor(pos); idx1 = next frame index after idx0:
             loop==fwd : idx0/idx1 wrap into [loop_start, loop_end)
             loop==none: idx1 = min(idx0+1, len-1)
s0, s1     : samples decoded to s16 (s8: x<<8 ; u8: (x-128)<<8 ; s16: as-is)
interp     : s   = s0 + (((s1 - s0) * frac) >>> 16)          // s16 result
volume     : sv  = (s * vol) >>> 8                            // vol u8, 255 ~ unity
pan        : l   = (sv * (255 - pan)) >>> 8                   // pan u8, 128 = center
             r   = (sv * pan) >>> 8
sum        : acc_l/acc_r += l/r  over all active voices       // s24 accumulator
master     : m_l = (acc_l * mvol) >>> 8                       // mvol u8
saturate   : out = clamp(m_l, -32768, 32767)                  // s16 to the DAC path
advance    : pos += step                                      // step u8.16 pitch
end        : loop==none && floor(pos) >= len        -> voice inactive
             loop==fwd  && floor(pos) >= loop_end   -> pos -= (loop_end-loop_start)<<16
```

## Fetch port (what P2's prefetch/DMA must serve)

The core asks for ONE decoded sample at a time; the wrapper owns memory access
and format decode (bench: behavioral RAM; P2: per-voice prefetch FIFOs fed by a
`LiteDRAMDMAReader`):

```
f_req  (out)  request strobe
f_addr (out)  BYTE address = base + idx * bytes_per_sample
f_fmt  (out)  0=s8 1=u8 2=s16le  (decode to s16 per the law above)
f_ack  (in)   data valid
f_data (in)   decoded s16
```

Two fetches per active voice per frame (s0, s1). Budget: 32 voices x ~10 cycles
<< the 1547-cycle frame at 74.25 MHz / 48 kHz.

## Config port (P2 maps this to the appended CSRs)

`cfg_sel` picks a voice; field writes to BASE/LEN/FMT/LOOP_START/LOOP_END stage
into shadows and apply atomically on CTRL.key_on (glitch-free, spec R8);
STEP/VOLPAN/MASTER write live (latched at the voice's next service slot).
CTRL: bit0 key_on (commit shadows, pos=0, active), bit1 key_off (immediate),
bit2 key_off at loop end (loop->none). `active_mask` is a NV-bit readback;
`pos_rd` returns the selected voice's integer position.

## P1 scope notes

- Loop modes: none + forward (spec R5 mandatory). Ping-pong: P2 if cheap.
- NV is a parameter (=32); the bench exercises 12 voices across every format,
  pitch up/down, loop/one-shot, pan extremes, and saturation.
- Cost target: ~1 DSP (shared multiplier), state in MLAB/M10K, small FSM.
