# sdl2-tetris on the riscv-stack — porting notes

Upstream: [github.com/andwn/sdl2-tetris](https://github.com/andwn/sdl2-tetris)
@ `0556543` (MIT — see `LICENSE`). ~800 lines of C99 against SDL2 + SDL2_ttf.

This port's real product is **`sdk/sdl2_lite.{h,c}`** — the SDK's SDL2 compat
layer (sibling of `sdl_lite`, the SDL-1.2 shim), sized to what this game
actually uses and designed to grow with the next SDL2 port. Tetris is the
proof.

## Status

- **Works (verified on the PC twin + a scripted headless HAL harness):**
  full game loop at 60 fps — move / rotate both ways / soft drop / hard drop /
  hold / ghost piece / line clears / levels / pause / game over / restart;
  text HUD (score, level, next, total); **persistent high score** through
  `save_open`/`save_commit` (Pocket keeps it in
  `Saves/riscv_stack/<game>.sav`; PC twin writes `./hiscores.sav`), restored
  at boot and shown as `Best:`.
- **Not exercised yet:** the real console build (no RISC-V toolchain on this
  machine) — see the checklist below.
- **Dropped:** upstream's `data/DejaVuSerif.ttf`. The shim's `TTF_*` corner
  renders with the SDK's built-in public-domain 8x8 bitmap font, so the game
  needs **no pak at all** (nothing to pick in the Pak slot; `pak_open`
  is never called). This was the "embed tiny assets" fork of the
  pakfs-vs-embed decision — there was nothing left to pack.
- Game has no audio upstream; none was added (and sdl2_lite deliberately has
  no audio API yet — first port that needs it defines its shape; hal.h
  `pcm_play`/`audio_stream_*` are available meanwhile).

## Build & run

```sh
cd sdk/tetris
make tetris-pc     # PC twin (needs desktop SDL2); ./tetris-pc to play
make               # console: tetris.bin (needs the SoC build tree
                   #   soc/build/pocket + the RISC-V toolchain, like every
                   #   game.mk target; guarded with a clear message if absent)
```

PC twin keys (hal_pc's fixed pad map): arrows = d-pad, `Z`=A, `X`=B, `A`=X,
`S`=Y, `Q`/`W`=L1/R1, `TAB`=SELECT, `ENTER`=START, `ESC` quits.

## Controls (pad map, set in `tetris.c:main`)

| Pad | Game |
|---|---|
| d-pad left/right | move (with upstream's delayed auto shift) |
| d-pad down | soft drop |
| d-pad up / A | rotate CW |
| B | rotate CCW |
| X or R1 | hard drop |
| Y or L1 | hold |
| START | pause / restart after game over |
| SELECT | quit to the game picker |
| SELECT+START | SDL_QUIT (same exit path) |

## How the seam works (the tyrian recipe, smaller)

- Vendored sources in `src/` still say `#include <SDL2/SDL.h>`;
  `compat/SDL2/{SDL.h,SDL_ttf.h}` shadow those includes (the Makefile puts
  `-Icompat` first) and route to `sdk/sdl2_lite.h`.
- **Link namespace:** every sdl2_lite export is `RVSDL2_`-prefixed via
  macros in the header — unconditionally, unlike sdl_lite's PC-only `RVL_`
  renames — so desktop libSDL2 keeps `SDL_*` for `hal_pc.c`. The `tetris-pc`
  target compiles in two groups: game+shim never see the real SDL2 headers,
  `hal_pc.c` only sees them.
- Layout: `BLOCK_SIZE` 16 → 10 turns the 22x24-block screen (352x384) into
  220x240; sdl2_lite centers it on the 320x240 panel. Window sizes that
  don't fit are a hard `SDL_CreateWindowAndRenderer` failure — the shim has
  no scaler, ports adjust their layout (that's the honest contract).
- All divergences from upstream are marked `RVSTACK` in-source; the vendor
  commit (`tetris: vendor sdl2-tetris sources`) is pristine upstream, so
  `git diff bd43e59..HEAD -- sdk/tetris/src` is the whole port diff.
  That diff also fixes four upstream bugs found while porting: `key`/
  `oldKey` defined in a header (pre-gcc-10 `-fcommon` relic), `clear_row`
  zeroing column 0 instead of the vacated top row, `detect_tspin`'s
  comma-operator indexing (T-spin detection never fired), and `level = 0`
  on reset making every line clear worth zero points.

## sdl2_lite: covered / not covered

Covered (see `sdk/sdl2_lite.h` for the full doc):

- init/quit/error: `SDL_Init` (runs `sys_init`), `SDL_Quit` (→ game picker),
  `SDL_GetError`
- video: `SDL_CreateWindowAndRenderer` (≤320x240, letterboxed),
  `SetWindowTitle`/`DestroyWindow`/`DestroyRenderer` (no-ops),
  `Set/GetRenderDrawColor`, `RenderClear`, `RenderFillRect`,
  `RenderCopy` (unscaled, byte 0 transparent), `RenderPresent`
- textures/surfaces: `CreateTextureFromSurface`, `QueryTexture`,
  `DestroyTexture`, `FreeSurface` (8bpp indexed surfaces)
- events/input: `PollEvent` (pad edges → SDL2 key events, SELECT+START →
  `SDL_QUIT`), `GetKeyboardState` (scancode-indexed), `RVSDL2_SetPadMap`
- time: `GetTicks`, `Delay` (keeps deferred flips alive)
- SDL2_ttf: `TTF_Init/Quit/GetError/OpenFont/CloseFont`,
  `TTF_RenderUTF8_Blended` (built-in 8x8 font, ASCII, ptsize ignored)

The core mechanism: **draw-color quantization**. SDL2 renders RGBA; the
console is 8-bit palettized. The shim allocates a palette slot per distinct
RGB on first use (entry 0 reserved as transparent), nearest-match past 255
colors. Perfect for flat-color games of this class; a >255-color game
belongs on sdl_lite's indexed surfaces instead.

Not covered (deliberately, until a port needs it): audio, `RenderCopy`
scaling, `SDL_CreateTexture`/`LockTexture` streaming, draw-line/point,
joystick/mouse APIs, multiple windows, `SDL_image`, real TTF rasterization,
renderer logical size. Extend the shim when the next game demands one of
these — implement it well or not at all.

## Console integration checklist

Written by analogy to `sdk/tyrian` (the console build was **not** run here —
no RISC-V toolchain on this box):

1. `cd sdk/tetris && make` on a machine with the SoC build tree
   (`soc/build/pocket`) and toolchain. Watch for:
   - `-std=gnu99` strictness (the known typedef-redefinition traps were
     already patched out of the vendored sources);
   - `font8x8_basic.h` defines its array (no `static`) — it is included by
     `sdl2_lite.c` only; nothing else in this binary may include it.
2. Copy `tetris.bin` to the SD, pick it in the **Game** slot. No Pak needed —
   leave the Pak slot as-is (the game never calls `pak_open`).
3. First boot: expect `Best: 0`, `save_restore_code()` paths untested for
   this title — a game over should create `Saves/riscv_stack/tetris.sav`
   (8-byte `TTRS` record inside the save window).
4. Sanity on hardware: pause (START) and hold-piece behavior exercise the
   time-gated `input_poll` in `SDL_PollEvent`; the game paces via both
   `fb_present()` vsync and its own `SDL_Delay` (clamped — see
   `graphics_flip`), so 60 fps should be automatic. Full-frame cost is one
   220x240 memcpy + palette reload — well inside the 50 MHz budget
   (tyrian's 320x200 copy was ~2 ms).
5. If the panel shows garbage colors: suspect palette timing —
   `RenderPresent` reloads the palette only on the frame a NEW color first
   appears (`pal_dirty`), which is worst-case one glitch frame; move the
   `palette_set` after `fb_present()` if it's visible in practice.
6. Optional polish later: route `SDL_RenderPresent` through the blitter
   (`HAL_FEAT_BLIT`) like `SDL_lite_present_indexed` does — not worth it
   until profiled.
