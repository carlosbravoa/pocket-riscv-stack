# Fixed Timebase — decoupling game timing from the CPU clock

**Status:** design (target v0.22.0, first move of the 1.0 ABI freeze)
**Problem it solves:** game `.bin`s currently derive wall-time from the *CPU*
clock, so changing `sys_clk` (e.g. 66 → 74.25 MHz) silently mis-times every game
until all bins are recompiled. This makes the timebase part of the console spec
instead, tied to a fixed crystal domain — after a one-time switch, game timing is
invariant to whatever the CPU runs at, forever.

## Background — how timing works today

`sys_ticks_us()` reads `timer0.uptime`, a counter clocked by **`cd_sys`** (the
variable CPU clock), then divides by the compile-time `CONFIG_CLOCK_FREQUENCY`:

```c
uint64_t c = timer0_uptime_cycles_read();          // counts sys_clk cycles
return (uint32_t)(c / (CONFIG_CLOCK_FREQUENCY / 1000000));
```

Because `CONFIG_CLOCK_FREQUENCY` is baked into each bin at compile time, a bin
built for 66 MHz runs ~12.5% fast on 74.25 MHz hardware in any tick-based logic
(gravity, frame caps, timeouts). Video (60 Hz) and audio (48 kHz) are already
**immune** — they are paced by the fixed **`cd_vid`** domain (12.288 MHz, derived
from the Pocket's `clk_74a` reference), not by `sys_clk`. This design simply
extends that same principle to game *logic* timing.

## The Pocket reference

The APF hands every core `clk_74a` — a 74.25 MHz crystal-derived reference,
stable regardless of our CPU clock. Our CRG already derives `cd_vid` (12.288 MHz)
from it for scanout and the 48 kHz audio drain (`12.288e6 / 256 = 48000` exact).
That fixed domain is the timebase source.

## RTL

A free-running counter in `cd_vid`, gray-coded across the CDC into `cd_sys` so the
CPU always reads a coherent value (at most one bit in flight). Lives in the `main`
`LiteXModule`, next to `vblank` / `audio_sample`, so it is **family-identical by
construction** (both flavors share that module).

```python
from migen.genlib.cdc import GrayCounter, GrayDecoder

# --- Fixed timebase: CPU-clock-INDEPENDENT wall clock -----------------------
# Counts the 12.288 MHz video/audio crystal domain, NOT sys_clk. Games read
# this, so their timing never shifts when sys_clk does. Gray-coded across the
# CDC -> the CPU always samples a coherent value.
TB_HZ = 12_288_000
tb = ClockDomainsRenamer("vid")(GrayCounter(32))
self.submodules.tb = tb
self.comb += tb.ce.eq(1)                             # +1 every 12.288 MHz tick

tb_gray_sys = Signal(32)
self.specials += MultiReg(tb.q, tb_gray_sys, "sys") # gray bits -> sys
self.submodules.tb_dec = GrayDecoder(32)            # decode in sys
self.comb += self.tb_dec.i.eq(tb_gray_sys)

self.tb_cycles = CSRStatus(32)                      # read anytime, no handshake
self.tb_hz     = CSRStatus(32, reset=TB_HZ)         # rate is discoverable
self.comb += self.tb_cycles.status.eq(self.tb_dec.o)
```

Two new CSRs: `main_tb_cycles`, `main_tb_hz`. Cost ~tens of LUTs.

## HAL

Signatures unchanged, so no game source changes. `CONFIG_CLOCK_FREQUENCY` leaves
the timing path entirely.

```c
uint32_t sys_ticks_us(void) {
    uint32_t hz = main_tb_hz_read();                // 12_288_000 (cache once)
    uint32_t c  = main_tb_cycles_read();
    return (uint32_t)(((uint64_t)c * 1000000u) / hz);
}
uint32_t sys_ticks_ms(void) {                       // 12288 cyc = 1 ms exactly
    return main_tb_cycles_read() / (main_tb_hz_read() / 1000u);
}
```

Reading `tb_hz` instead of hardcoding means even a future crystal-domain change
is discovered at runtime, never recompiled.

## Tradeoffs / decisions

- **Wrap — WIDTH MATTERS, learned the hard way (v0.22.0 field bug).** A 32-bit
  counter at 12.288 MHz wraps every ~349 s (5.8 min). Delta timing (`now - t0 < x`)
  is wrap-safe, BUT games with **absolute** timing (`while (SDL_GetTicks() < target)`,
  e.g. OpenTyrian's frame pacing) hang for a full wrap whenever `target` straddles
  the boundary — image freezes, audio keeps pumping (the wait loop pumps it), then
  it self-recovers minutes later. The old timer0 wrapped at ~71 min so this was too
  rare to ever see; 32-bit made it frequent. **Ship ≥40 bits.** We use **40** here:
  the counter wraps only every ~24.8 h, and `sys_ticks_us` reaches its natural
  uint32 2^32-µs (~71 min) wrap, matching the pre-timebase timer. Read as two CSR
  words (`tb_cycles` low 32 + `tb_cycles_hi` high 8) with a coherent hi/lo/hi
  re-read. 40 is the max width that keeps the µs reciprocal (`c * 1365333`) inside
  uint64; go wider (48) only with a scaled-down reciprocal.
- **CDC:** gray-code needs no handshake — the CPU just reads. A latch CSR (like
  `timer0.uptime_latch`) is the alternative but adds a write-then-read round trip.
- **Precision:** 12.288 MHz is not integer-µs, so µs uses a fixed multiply (the
  constant never changes again); ms is exact (`/12288`).

## Sim

The full-system sim already models `cd_vid` (video works in sim), so the counter
advances there. Add one assertion in `tb_core_top.cpp` that `tb_cycles` increments
at the 12.288 rate.

## Migration

1. Add the RTL + CSRs (both flavors — automatic via `main`).
2. Switch the HAL `sys_ticks_*` to the new CSR; keep `timer0` for anything
   genuinely CPU-cycle-scoped (there is nothing today).
3. Rebuild both bitstreams once + rebuild all game bins once to adopt it.
4. Freeze `TB_HZ = 12_288_000` into the console spec — permanent.

After step 4, **no game bin is ever recalibrated for a clock change again**, and
`base@66 MHz` + `FM@74.25 MHz` can ship **identical** bins.
