# RISC-V Stack v0.16.0 — Beta 2: The Family

A from-scratch game console for the Analogue Pocket, now in **two flavors that
coexist on one SD card** and run the **same game binaries**:

| Download | Core | What it adds |
|---|---|---|
| `RiscvStack_v0.16.0.zip` | RiscvStack | the base console |
| `RiscvStackFM_v0.16.0.zip` | RiscvStackFM | a real **hardware OPL3** FM synthesizer (AdLib-style music) |

Both share one platform ("RISC-V Stack"), one `Assets/riscv_stack/common/`
directory, and one SDK. Games detect the hardware at runtime
(`sys_caps()->features & HAL_FEAT_FM`) and adapt — `fmdemo.bin` is a playable
FM piano on the FM core and says so politely on the base core.

## Install

1. Unzip either (or both!) zips at the root of your Pocket SD card.
2. Open the core → bootloader appears → Pocket menu → **Game** slot → pick a
   `.bin` (`pong.bin`, `demo.bin`, `fmdemo.bin` — included).
3. Picking a different game reboots straight into it. SELECT+START exits a
   game back to the picker.

## The console (both flavors)

- VexiiRiscv (rv32im+zicbom) @ 50 MHz, 64 MB SDRAM, 16 KB fast SRAM stack
- 320×240 8-bit **palettized** video, double-buffered, tear-free @60 Hz,
  256-entry hardware palette (fades/flashes for free)
- 48 kHz stereo PCM (stream or 4 mixed voices) — plus hardware FM on the
  FM flavor
- Two controllers (+ analog values), 4 KB battery-style save memory with
  on-demand SD persistence (`save_flush()`), asset loading from SD (pak)
- Games are plain files: `make` → copy `.bin` → play. No FPGA toolchain.

## SDK

`riscv-stack-sdk.tar.gz` — standalone: needs only `riscv-none-elf-gcc` on
PATH. Unpack, `cd examples/pong && make`. Guide included (`GUIDE.md`);
`pong` is the worked example (states, physics, palette FX, SFX, saves).

## New since Beta 1 (v0.12.1)

- Hardware palette + `palette_set()` (pong: fade-in, red miss-flash)
- Saves: 4 KB persisted to SD, flushed at game exit / on demand; pong keeps
  its best score
- Game exit protocol (SELECT+START → picker), analog stick support,
  `pcm_play()` voices + `audio_pump()`, `malloc()` in games
- Standalone SDK export; runtime capability detection (one binary, any flavor)
- **The FM flavor**: opl3_fpga (LGPL) integrated at 12.288 MHz — after a
  memorable synthesis war, shipped as a pre-synthesized netlist; the battle
  and the regeneration procedure are documented in `soc/pocket_core/opl3/`

## Beta status / known limits

- Saves and flavor-coexistence are freshly built and not yet re-verified on
  hardware (everything else in this list is hardware-confirmed)
- Fixed 48 kHz audio stream; games don't return (re-pick or exit); pak files
  need ≥2 bytes of padding (APF quirk; the SDK pads game binaries)
- The two flavors share the 4 KB save memory — records are magic-tagged
