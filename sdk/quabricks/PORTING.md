# Quabricks on the riscv-stack — porting notes

Upstream: the revamped **sdl2-tetris**, renamed **Quabricks** (MIT — see
`LICENSE`), vendored @ `513c67b` ("Rename to Quabricks; add lose animation,
high scores; fix top-out bug"). ~2000 lines of C99 against SDL2 + SDL2_ttf +
SDL2_mixer. This port **replaces `sdk/tetris`** (the pre-revamp port, branch
`port/tetris`), which is obsolete.

## Stage-0 verdict (written before code)

Green flags: C99, single-threaded 60 Hz frame loop, flat-color 2D that
quantizes, tiny persistent state (five-entry score table), assets are 13
procedurally-generated WAVs (~210 KB) + three fonts we don't need. Risks
called out up front: (1) upstream now renders at **776x764** with
alpha-heavy "modern" UI (gradients, translucent panels, SDL_RenderGeometry)
— needs a full re-layout plus shim growth for blending; (2) **SDL2_mixer**
SFX — sdl2_lite had no audio at all; (3) floats in draw paths
(sqrtf/cosf/sinf per frame) on an FPU-less core. All three were judged
shim-or-seam work, not game-logic surgery: PORT.

## Status

- **Works (PC twin, verified headless + interactive):** full game at 60 fps —
  title / level-select / stage / pause / lose-sweep / game-over screens,
  ghost piece, hold, 5-slot queue, T-spin scoring, line-clear flash,
  hard-drop shake, drifting menu blocks, 13 SFX, **persistent five-entry
  high-score table with pad-only initials entry**. Upstream's `--selftest`
  passes (clear_row / lockout / leveling / autoplay-to-gameover).
- **Console `.bin` links** against the real SoC tree + xpack RISC-V gcc
  (288 KB, SFX embedded). Not yet booted on sim or hardware.
- **Sim gate skipped** (time): `SKIP_SOC=1 GAME=quabricks ./run_sim.sh`
  needs a run_sim.sh case in the main tree — do this before hardware.
- **Dropped:** upstream `data/*.ttf` (~2 MB Lato) — the shim's built-in
  8x8 font renders all text, integer-scaled (8/16/24/32 px). `--shots`
  screenshot writing (no ReadPixels in the shim; use `RVSTACK_SHOT`).
  **No pak at all**: fonts are built-in, SFX are compiled in.

## Build & run

```sh
cd sdk/quabricks
make quabricks-pc    # PC twin (desktop SDL2); ./quabricks-pc to play
make                 # console: quabricks.bin (needs soc/build/pocket +
                     #   toolchain; BUILD_DIR=... to point elsewhere)
make sfx             # regenerate compat/sfx_data.* from data/sfx (python3)
```

Headless verify (what CI/gates should run):

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./quabricks-pc --selftest
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  RVSTACK_INPUT="30:T,60:T,100:X,300:S,306:T" \
  RVSTACK_SHOT="240:stage.bmp" ./quabricks-pc
```

## Controls (pad map, set in `tetris.c:main`)

| Pad | Game |
|---|---|
| d-pad left/right | move (upstream's delayed auto shift kept) |
| d-pad down | soft drop |
| d-pad up / A | rotate CW |
| B | rotate CCW |
| X or R1 | hard drop |
| Y or L1 | hold |
| START | pause / confirm menus / commit initials |
| SELECT | back / quit-to-title |
| SELECT+START | quit to game picker (saves flushed first) |

Name entry is pad-only: three arcade initials, left/right picks the slot,
up/down cycles A-Z 0-9 `.` `-` space, START commits (upstream captured
SDL_TEXTINPUT from a keyboard).

## Layout: the full 320x240

Upstream drew a 776x764 desktop window. `BLOCK_SIZE` 32 -> **11** makes the
well 110x220, centered, with symmetric 94 px cards: HOLD + stats
(score/level/lines/next-in) + pad legend on the left, the 5-piece NEXT queue
on the right, 4 px margins — **zero letterbox**. The menu screens were
recomposed around the same 320x240 (`SCREEN_W/H` are no longer derived from
the stage) and letterbox-degrade gracefully if a future flavor is taller:
everything centers on `SCREEN_W/2` and hangs off `SCREEN_H` at the bottom.
The big score header didn't survive the shrink — score lives in the left
stats card.

## Sound (the new ground — sdl2_lite grew audio here)

Upstream: SDL2_mixer, `Mix_LoadWAV` of 44.1 kHz WAVs, callback-threaded.
Port: `sdk/sdl2_lite` now carries a **Mix_-shaped subset** (OpenAudio /
QuickLoad_RAW / PlayChannel / FreeChunk / CloseAudio) over an 8-channel
one-shot software mixer, pumped by `RVSDL2_AudioPump()` from
RenderPresent/Delay — **only ever `audio_stream_free()` frames, never
blocking** (PORTABILITY.md trap #6; modeled on `SDL_lite_audio_pump`).
Chunks are **mono s16 @ 48 kHz by contract**: `tools/wav2c.py` resamples the
WAVs at build time into `compat/sfx_data.c` (~227 KB, checked in), so there
is no runtime WAV parser, no resampler, no filesystem, no pak.
`compat/SDL2/SDL_mixer.h` shadows the include; `sound.c` diverges from
upstream only in `Mix_QuickLoad_RAW` vs `Mix_LoadWAV` (RVSTACK-marked).

## High scores (save budget math)

`highscores.txt` + fopen/fprintf/sscanf -> `save_open("hiscores", ...)` +
`save_commit` (`hiscore.c`, RVSTACK-marked; API unchanged). The blob is
`12 + 5*(16+12) = 152 bytes` of the **32 KB** per-game save window — 0.5%.
`save_open` once at boot, `save_commit` on record insert and again on the
quit path (`main` flushes before `graphics_quit`, whose `SDL_Quit` is
`sys_exit` on console). Pocket: `Saves/riscv_stack/<game>.sav`; PC twin:
`./hiscores.sav`. Verified round-trip on the PC twin (BEST line on title
after restart).

## The old port's five bug fixes, re-checked against the revamp

| port/tetris fix | Revamp status |
|---|---|
| `detect_tspin` comma-operator indexing | **fixed upstream** (proper `cell_occupied`) |
| `reset_game` set `level = 0` (zero scoring) | **fixed upstream** (`level = 1`) |
| `clear_row` zeroed column 0, not the top row | **fixed upstream** (+ selftest covers it) |
| `key`/`oldKey` defined in input.h | **fixed upstream** (defined in input.c) |
| `SDL_Delay` underflow on a >16 ms frame | **fixed upstream** (`long wait; if(wait > 0)`) |

(The "top-out bug" in the upstream log is the old port's lockout corruption,
also fixed — `lock_piece` never writes negative rows and has a selftest.)
Nothing to re-apply; the replayed **seam** lessons instead: BLOCK_SIZE
re-derivation, the pad map, logsys via the fputs pattern (picolibc has no
v*printf), `srand(sys_ticks_us())` at boot + game start (gamelib provides
rand/srand — never redefine), shadow headers with two compile groups,
`hal.h` first in any file that uses it.

## What changed vs the old tetris port (beyond upstream's revamp)

- **sdl2_lite grew** (see its header doc): BLEND draw mode (src-over vs the
  palette, 5-bit re-quantized, per-(color,dst) LUT), `SDL_RenderDrawPoint`,
  `SDL_InitSubSystem`/`SDL_SetHint` no-ops, TTF integer scaling +
  `TTF_FontHeight`, and the whole mixer corner above.
- **graphics.c is float-free** (RVSTACK): integer `isqrt32` for rounded
  corners, midpoint-circle arcs instead of 91-step cosf/sinf outlines, an
  integer scanline `graphics_fill_triangle` replacing `SDL_RenderGeometry`,
  and the background gradient banded to <= 16 steps (each distinct row color
  costs a palette slot on the console).
- Translucent **text** colors are pre-blended toward the backdrop tone in
  `graphics_text` (shim text textures are opaque-where-inked).
- `compat/libc_shim.c` (console-only): `snprintf`/`vsnprintf` (%d %i %u %x
  %c %s), trimmed from tyrian's shim — picolibc-minimal has neither.
- 220x240-with-bars -> full 320x240; single best-score -> five-entry table
  with initials; silent -> 13 SFX.

## Console checklist (for the maintainer)

1. Add a `quabricks` case to `soc/sim/run_sim.sh`, boot
   `SKIP_SOC=1 GAME=quabricks ./run_sim.sh`, watch for `0xDEAD____` diags.
   No pak is requested — no `[HOST] read` lines expected.
2. Copy `quabricks.bin` to SD, pick in the **Game** slot, leave Pak alone.
3. First boot: title shows no BEST; one game over should create
   `Saves/riscv_stack/quabricks.sav` (152-byte `QBRK` blob).
4. Audio: SFX should be crisp with flat ~42 ms latency. If it crackles,
   suspect the pump starving on long frames — `RVSDL2_AudioPump()` can be
   called from any hot loop safely.
5. Perf: worst frame is the stage full of blocks (~220 beveled cells +
   blends + full-frame present memcpy). If it misses 60 fps, profile
   (`RVSTACK_PROFILE=1`) — `graphics_draw_block`'s gloss (blended) is the
   first suspect; a per-(color,size) cell cache is the ready fix.
6. Palette: alpha art allocates blend slots lazily; overflow degrades to
   nearest-match. If menus glitch colors on frame 1-2, it's `pal_dirty`
   timing (one-frame worst case, same note as the old port).
