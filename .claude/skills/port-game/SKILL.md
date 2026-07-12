---
name: port-game
description: Port an SDL-era game (or evaluate a candidate) to the RISC-V Stack console. Use when asked to port a game, judge portability, or debug a port that works on PC but fails on console/sim.
---

# Porting a game to RISC-V Stack

**Read `sdk/PORTABILITY.md` first** — it is the contract: hardware envelope,
green/red flags, the eight platform traps, and the four-stage workflow. This
skill is the operational checklist on top of it.

## Stage 0 — candidacy (10 minutes, before any code)

Score the game against PORTABILITY.md's flags. Verify: language (C), SDL
dialect (1.2 → `sdl_lite`; SDL2 → `sdl2_lite`, extend it only for what the
game uses), render model (8-bit palettized? truecolor with few flat colors
quantizes; photographic truecolor does not), math (grep for `float`/`double`
in inner loops), threads (`SDL_CreateThread`, audio assumptions), asset
license and size, savegame size vs the 32 KB budget. Write the verdict down
in the port's `PORTING.md` before starting.

## Stage 1 — scaffold (copy, don't invent)

- `sdk/<game>/src/` = pristine vendored upstream + license file. Every local
  change marked `RVSTACK:` so `git diff` of src/ IS the port.
- `sdk/<game>/compat/` = the seam. Steal from the closest worked example:
  Tyrian (SDL-1.2 + OPL music + pak + saves), Doom (hal.h-direct via a tiny
  platform API), Wolf3D (SDL2-shaped + shadow-stdio + IMF/OPL), Tetris
  (sdl2_lite). Shadow headers (`compat/stdio.h`, `compat/SDL.h`) reroute
  includes; keep two compile groups so shadow headers never touch SDK/PC
  sources.
- Makefile: console target via `include ../game.mk` guarded on
  `$(BUILD_DIR)/software/include/generated/variables.mak` existing (honor
  `BUILD_DIR ?=`), plus a `<game>-pc` PC-twin target and a `pak` target
  (`soc/tools/make_pakfs.py`).

## Stage 2 — the four gates, in order

1. **PC twin plays** (desktop; use the RVSTACK_* env instruments; headless
   smoke = `SDL_VIDEODRIVER=dummy` + `RVSTACK_INPUT` + `RVSTACK_SHOT`).
2. **Console links** (`make` with the real toolchain) — expect picolibc
   gaps; shim them in `compat/libc_shim.c`/`math_shim.c`, and check gamelib
   first (it already has malloc/mem*/rand). Never define a symbol gamelib
   already provides.
3. **Sim boots** (`SKIP_SOC=1 GAME=<game> ./run_sim.sh`, add a pak case in
   run_sim.sh): watch for `0xDEAD____` diags (trap + mcause) and confirm the
   pak is requested ([HOST] read lines). Add `rvb_progress` boot beacons —
   they are the difference between "black screen" and a line number.
4. **Hardware**: red bars = CPU trap (usually alignment — trap #3 in
   PORTABILITY.md); black screen = system hang; garbled audio = pump
   discipline (trap #6).

## Stage 3 — platform conventions (make it feel native)

- Full 320x240 when the game allows; 320x200 letterboxed only when authentic.
- Pad map documented in PORTING.md; SELECT+START = quit-to-picker (flush
  saves first: `save_commit` then `sys_exit`).
- Saves through `save_open`/`save_commit`; state the budget math in
  PORTING.md.
- Music: if the game has OPL/IMF/MIDI-era music, route register writes to
  `opl_write` gated on `HAL_FEAT_FM` — never synthesize FM on the CPU.
- `PORTING.md` = what works, what's stubbed, asset pipeline, console
  checklist, deviations. It is the deliverable reviewers read.

## Debugging map (symptom → tool)

| Symptom | Tool |
|---|---|
| Works on PC, traps on hardware | sim + diag mcause; suspect alignment, then stack frames |
| Works on PC, wedges on hardware/sim | boot beacons; check pak actually requested |
| Audio stutters/starves | `audio_stream_free()` discipline; A-number in the HUD |
| Slow | RTL profiler (`RVSTACK_PROFILE=1`, commit-PC histogram), then LUT/hoist the top bucket — prove rewrites bit-exact with a native harness first |
| Music wrong on FM | `RVSTACK_OPLLOG` on PC = reference stream; compare |
