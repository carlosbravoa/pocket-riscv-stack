---
name: port-game
description: Port an SDL-era game (or evaluate a candidate) to the RISC-V Stack console, re-sync an existing port from its upstream game repo and rebuild the console .bin, or debug a port that works on PC but fails on console/sim. Use when asked to port a game, "build the Pocket version" of a game, judge portability, or fix a slow/broken port.
---

# Porting a game to RISC-V Stack

**Read `sdk/PORTABILITY.md` first** — it is the contract: hardware envelope,
green/red flags, the eight platform traps, and the four-stage workflow. This
skill is the operational checklist on top of it.

## Re-syncing an existing port ("build the Pocket version") — the fast path

Ports live in a SEPARATE repo from their upstream game: game logic is developed
upstream (e.g. `~/devel/games/sdl2-tetris`), the console adaptation lives in
`sdk/<game>/` on a `port/<game>` branch. When the upstream game changes and you
just need a fresh `.bin`:

1. **Read the port's `SYNC.md`** (if present) — it classifies SHARED game-logic
   files vs PORT-OWNED presentation/platform, and lists the seams.
2. `diff -u <upstream>/<file> sdk/<game>/src/<file>`: **logic** changes copy
   across unchanged; **presentation** changes (HUD layout, a new `graphics_*`
   call) are re-applied by hand for the indexed/blitter renderer. Per-platform
   numbers stay in `config.h` — never copy those across.
3. Build: `make -C sdk/<game> BUILD_DIR="$PWD/soc/build/pocket"` (+ the PC twin
   `make -C sdk/<game> <game>-pc` for a fast visual check).
4. Deliver the `.bin` (drop-in, no reflash); record the upstream commit synced
   to in `PORTING.md` so the next diff has a clean base.

## Performance — read `sdk/ACCEL.md` (the renderer is where ports die)

Non-negotiables (Quabricks paid for these; a port can be logically perfect and
still miss 60 fps):

- Present via the blitter (`SDL_lite_present_indexed` / `sdl2_lite`'s DMA
  present + `fb_present_dma`); never a per-row CPU copy of the frame.
- No per-frame full-screen redraw and no `SDL_RenderClear` you immediately
  overpaint; composite static UI ONCE, redraw only what changed.
- No translucent (`alpha<255`) fills over static backgrounds — a per-pixel
  software blend over ~95k px alone blows the ~1.24M-cycle frame budget. Bake
  the blended-over-static result to an opaque constant.
- **If accelerating the present changes nothing, profile the DRAW loop, not the
  copy** — the cost is upstream in how the frame is composed.

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
