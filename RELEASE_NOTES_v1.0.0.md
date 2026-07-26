# RISC-V Stack v1.0.0 — the stable console platform

A from-scratch Analogue Pocket game console: a LiteX SoC around a VexiiRiscv
(rv32im) CPU, running games as portable C `.bin` files loaded from the SD card
into DRAM. v1.0 is the point where the platform stops moving under your feet.

## What 1.0 means: the ABI is locked

The console's register map and HAL are frozen as **ABI v1**. A game `.bin` built
against v1 runs on this bitstream and every future one — new hardware only ever
*appends* to the map, guarded automatically so a change can never silently break
an existing binary (`soc/abi/`). Both flavors report their version through the
new `abi_version` register (`sys_abi_version()` → `1.0`).

## One install, two flavors — byte-identical ABI

The family zip carries both cores; unzip at the SD-card root and pick the one you
want in the Pocket menu:

- **RiscvStack** — the base console.
- **RiscvStackFM** — adds a hardware **OPL3** FM synth (+ a polyphase FIR
  resampler, 49548→48000 Hz) for authentic AdLib/Sound-Blaster music.

They share the same SoC, CSR map, and games — the only difference is the audio
block. A game built once runs on both and adapts at runtime via the capability
bits (`sys_caps()->features`).

## Platform highlights

- **74.25 MHz** VexiiRiscv (rv32im), external DRAM game region.
- **Hardware 2D blitter** — DRAM-speed rectangular copy (~14× a CPU copy),
  asynchronous, with a **colorkey** sprite mode. The SDK's `ACCEL.md` is the
  guide to using it; the `sdl_lite`/`sdl2_lite` shims route their present through
  it automatically.
- **CPU-clock-independent timebase** — a 40-bit counter off the 12.288 MHz video
  domain backs `sys_ticks_us()`, so timing is correct regardless of CPU clock.
- **Nonvolatile per-game saves** — one `.sav` per game, created and persisted by
  the Pocket itself (the SNES-core mechanism).
- **Pak auto-load** — big data paks open by name, no manual second slot pick.
- **Console UX** — loading screen, palette reset on exit, and a Pocket-menu
  "Exit to Menu" that returns to the game picker.

## SDK — build your own games and ports

Everything a game needs sits on one header (`soc/hal/hal.h`). The SDK ships:

- the HAL + BSP, indexed 8bpp double-buffered framebuffer, input, FM/PCM/stream
  audio, saves, pak/pakfs asset loading;
- **`sdl_lite`** (SDL 1.2) and **`sdl2_lite`** (SDL2) shims so real game ports
  drop in with a small diff;
- a **PC twin** for desktop-speed iteration, a full-system **Verilator sim** that
  boots the real RTL, and guides: `GUIDE.md`, `ACCEL.md`, `PORTABILITY.md`.

Bundled games: `pong`, `demo`, `fmdemo`. Ports built on this SDK (Tyrian, Doom,
Wolfenstein 3D, Commander Keen, Quabricks, an OPL3 MIDI player) are distributed
as their own `.bin`s.

## Install

Unzip `RiscvStackFamily_v1.0.0.zip` at the root of the Pocket SD card. Both cores
appear under the openFPGA menu. Games (`.bin`) and optional data paks go in
`Assets/riscv_stack/common/`.

## Build integrity — new in 1.0

Every 1.0 bitstream is **byte-reproducible**: the build pipeline pins all
previously-hidden variable inputs (CPU netlist naming, build timestamps), and the
SDRAM clock now leaves the FPGA through an IO-element register instead of
fit-dependent fabric routing, with the DRAM capture phase measured on silicon
(both flavors: window {135°–165°}, shipped at 150°). The released bitstreams are
byte-identical to the exact cores that were hardware-verified. Full engineering
write-up: `soc/REPRODUCIBILITY.md`.

---
Verified in the full-system simulation and on hardware before release.
