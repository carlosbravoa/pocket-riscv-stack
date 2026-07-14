# FM FIR resampler (49548.4 -> 48000 Hz, exact 31/32)

Replaces the zero-order-hold latch in core_top's FM path with a polyphase FIR.
Steady-state alias floor **-97 dBc** (fixed-point) vs the ZOH's **-12.8 dBc** — ~85 dB
cleaner FM audio for every OPL title (Tyrian, Doom OPL, MIDI player, fmdemo).

- `gen_coeffs.py`  — designs the filter (16 taps/phase, Kaiser beta=9, cutoff Fout/2),
                     emits `fir_coeffs.hex` (496 x int16 Q15) + golden bench vectors.
- `fm_resample.v`  — the RTL: one 16x16 multiplier (1 DSP), 496x16 coeff ROM (1 M10K),
                     16-deep stereo delay line, input-driven 31/32 schedule.
- `fm_resample_tb.v` — Verilator bench, PASSES bit-exact vs the reference (0 mismatch).
- `fir_proto.py`   — the DSP analysis/plot (ZOH vs FIR spectra).

Cost: 1 DSP + 1 M10K + a few regs. Budget: 32 MAC/output in a 256-cycle window.
INTEGRATION (todo): in core_top, feed opl_sample_l/r[20:5] + opl_sample_valid to
fm_resample; take out_l/out_r into the FM mix (replacing the opl_l/opl_r ZOH latch);
add fm_resample.v + fir_coeffs.hex to ap_core.qsf. Rides the next FM bitstream.
