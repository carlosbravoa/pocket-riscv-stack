# RISC-V Stack v0.12.1 — First Beta

A from-scratch game console for the Analogue Pocket: a 50 MHz RISC-V soft CPU
(VexiiRiscv), 64 MB RAM, a tear-free 320x240 double-buffered framebuffer,
48 kHz stereo audio and controller input — programmable in plain C. **Games are
files on the SD card**; writing one never touches the FPGA toolchain.

## Install

1. Unzip `RiscvStack_v0.12.1.zip` at the root of your Pocket SD card.
2. Open **RISC-V Stack** (Utility) on the Pocket — you'll see the bootloader.
3. Pocket menu → **Game** slot → pick `pong.bin` (or `demo.bin`). It boots.
4. Optional: pick `demo.img` in the **Pak** slot for the demo's background
   (press X in the demo to load it).

Picking a different game from the menu reboots straight into it.

## Write your own game

```sh
cd sdk/pong && make        # -> pong.bin, copy to SD, pick it. That's all.
```

See `sdk/GUIDE.md` (programming guide) and `sdk/pong/main.c` (a complete
commented game: states, physics, text HUD, sound effects).

## What's inside

- **Core**: LiteX SoC — VexiiRiscv rv32im+zicbom @ 50 MHz, LiteDRAM SDR SDRAM,
  DRAM-scanout video with vsync'd page flip, i2s audio, deferred APF data-slot
  loading with DMA. ~28% of the FPGA used; timing signoff clean.
- **HAL** (`soc/hal/hal.h`): the whole game API — framebuffer, input, audio
  stream, pak files, timing.
- **SDK** (`sdk/`): linker script + crt0 + Makefile for DRAM-resident games,
  two examples (`demo`, `pong`), programming guide.

## Known limitations (beta)

- Fixed rgb332 color (palette RAM planned); 48 kHz-only audio stream.
- SDK builds in-repo (needs the SoC build tree); standalone export planned.
- Games don't exit (re-pick or power off); saves not yet implemented.
- Pak files must be padded by >= 2 bytes (APF EOF quirk; `game.mk` handles
  game binaries automatically).

## v0.12.1 fixes over v0.12.0

- Picking a new game while one runs now auto-reboots into it.
- Fixed a slot-select race that made asset (Pak) pulls use the wrong size.
- The demo shows an on-screen pak status line.
