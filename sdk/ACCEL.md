# Making a game run at full speed — the 2D acceleration guide

The RISC-V Stack has a hardware **blitter**: a DMA engine that copies
rectangles of the framebuffer at DRAM speed instead of one pixel at a time on
the CPU. It is roughly **14× faster than a CPU copy for a full frame** and it
runs **asynchronously** — you kick it and keep computing. A game that ignores
it burns its whole frame budget shuffling bytes; a game that uses it spends
that budget on the game.

This guide is the checklist for reaching that fast path. If your port feels
slow (Quabricks did), you are almost certainly on the slow path described in
§2. Read §1, then jump to §6 for the mechanical fix.

> **For coding agents:** §7 is a step-by-step porting checklist you can execute
> top-to-bottom. The rest is the *why*.

---

## 1. The cost model (know the budget)

At 74.25 MHz you have **~1.24 million CPU cycles per 60 Hz frame**. The frame
is 320×240 = 76,800 bytes (8bpp indexed). What eats the budget:

| Operation | Cost per full frame | Notes |
|---|---|---|
| CPU per-pixel plot (`fb[y*w+x]=c`) | **catastrophic** | ~5–15 cyc/pixel × 76,800 = most of the frame for ONE pass |
| **CPU alpha-blend fill (`a<255`)** | **catastrophic** | ~10–15 cyc/pixel even on a warm blend LUT — a *single* translucent full-screen panel ≈ the whole frame budget |
| CPU `memcpy`/`memset` full frame | **~2 ms @ 50 MHz** (~1.5 ms @ 74) | the shadow-surface copy, the opaque fill |
| Full-frame dcache flush | ~0.5–1 ms | what plain `fb_present()` does every flip |
| **Hardware `blit()` full frame** | **~0.1–0.15 ms** | async — overlap it with logic |
| **Hardware `blit_ck()` sparse sprite** | cheaper still | fully-transparent 16-bit beats are skipped |
| `palette_set()` (fade/flash/cycle) | one 256×3 write | **zero** pixels touched |

The three rules that follow from this table:

1. **Never redraw what didn't change.** A Tetris board, a HUD, a static
   background — draw it once, blit it, leave it.
2. **Never touch a pixel on the CPU that the blitter could move.** Full-frame
   composition is the blitter's job.
3. **Never redraw for a color change.** Fades, flashes, damage-flash, and color
   cycling are `palette_set()` — they cost nothing per pixel.
4. **Never re-blend what didn't change.** A translucent panel over a static
   background is a *constant color* — bake it once, draw it opaque. Per-pixel
   alpha blending is as expensive as per-pixel plotting (see §5b).

---

## 2. Why the default SDL path is slow (the trap)

The `sdl_lite` shim gives you SDL-1.2 names so ports drop in with a small diff.
But the *default* present path is deliberately simple, not fast. When your game
does the classic SDL loop:

```c
SDL_FillRect(screen, &r, color);      // CPU memset into a shadow surface
SDL_BlitSurface(sprite, ...);          // CPU memcpy into the shadow surface
SDL_Flip(screen);                      // CPU memcpy shadow->fb, full flush, vsync
```

…every pixel is touched **twice on the CPU** (once drawing into the shadow
surface, once copying that surface to the framebuffer) and then the whole frame
is dcache-flushed. On a Tetris-class game that also recomputes every block —
including rounded-corner math — per frame, that is easily a 3–5× overshoot of
the frame budget. **This is the path Quabricks was on.**

Nothing here is wrong for correctness — it just leaves the blitter idle.

---

## 3. Three present paths (pick the fastest you can reach)

| Path | How you get it | Speed | Use when |
|---|---|---|---|
| **A. `SDL_Flip(screen)`** | default | slow (CPU copy + full flush) | prototype only |
| **B. `SDL_lite_present_indexed(pix, pitch, w, h, pal)`** | call it instead of `SDL_Flip` | **fast** (blit + `fb_present_dma`) | you keep your own stable indexed frame buffer |
| **C. raw `blit()` / `blit_ck()` + `fb_present_dma()`** | drop `sdl_lite`, use the HAL directly | **fastest, full control** | you compose the frame yourself |

**Path B is the cheapest win for an SDL port.** If your game already renders
into one contiguous `uint8_t` buffer (its own "screen"), hand that buffer to
`SDL_lite_present_indexed()` and you skip the entire shadow-surface copy — the
blitter moves your frame to the display and `fb_present_dma()` flips without the
full-frame flush. This is exactly what Tyrian does (its `VGAScreen`), and it is
why Tyrian runs *faster than the dedicated port*.

```c
// Path B: your game owns `framebuf` (w*h indexed bytes) and `palette` (256*3).
SDL_lite_present_indexed(framebuf, w /*pitch*/, w, h, palette);
// no SDL_Flip, no shadow surface, no per-frame full-screen CPU copy.
```

---

## 4. The blitter API and the rules it demands

From `hal.h` (gate everything on the capability bit — a flavor without the
blitter must still run your game via the CPU fallback):

```c
if (sys_caps()->features & HAL_FEAT_BLIT) {
    flush_cpu_dcache_range(src, (h-1)*src_stride + w_bytes);   // 1. flush source
    blit(dst, src, w_bytes, h_rows, src_stride, dst_stride);   // 2. kick (async!)
    // ... do other work here: the engine runs while the CPU does ...
    blit_wait();                                               // 3. sync before you read dst
}
```

**Colorkey copy** — the sprite/tile primitive. Source bytes equal to **0** (the
SDK-wide transparent index) are not written; fully-transparent 16-bit chunks
are skipped entirely, so sparse art is cheap:

```c
if (sys_caps()->features & HAL_FEAT_BLITKEY)
    blit_ck(dst, sprite, w_bytes, h_rows, src_stride, dst_stride);
```

**The five rules (violate one and you get garbage or a stale frame):**

1. **Colorkey index is 0.** Reserve palette entry 0 for transparency in your
   sprite art. Opaque black should be a *different* index.
2. **2-byte alignment** on `dst`, `src`, both strides, and `w_bytes`. Pad
   odd-width sprites to even.
3. **Flush the source first.** The engine reads DRAM directly and bypasses the
   CPU cache; a source the CPU just drew is still partly in cache. Flush the
   exact range you're blitting (`(h-1)*stride + width`).
4. **Never leave dirty cache lines over the destination.** If you also draw
   into `dst` on the CPU (an overlay), flush *that* range before presenting.
5. **Present with `fb_present_dma()`** when the blitter composed the frame — it
   skips the page-wide flush that plain `fb_present()` does. Any CPU-drawn
   overlay (HUD, score) must be range-flushed by you first.

**Async is the point.** `blit()` returns immediately. Kick the background copy,
then run game logic or build the next sprite list, and only `blit_wait()` right
before you depend on the result. Sequencing `blit(); blit_wait();` back-to-back
leaves half the win on the table.

---

## 5. Palette tricks (effects for free)

The framebuffer byte is an index into a 256-entry hardware palette. Changing the
palette recolors the *whole screen* without touching a pixel:

- **Fade in/out**: scale all 256 RGB triples toward 0 or 255 over N frames —
  one `palette_set()` per frame, no redraw.
- **Damage/pickup flash**: swap to a white-shifted palette for 2 frames, swap
  back.
- **Color cycling** (waterfalls, force fields, the Tyrian starfield): rotate a
  band of palette entries each frame.
- **Two "layers" in one buffer**: reserve an index range for UI so a full-screen
  effect on the play area leaves the HUD colors fixed.

Reload right after `fb_present()` for glitch-free timing. See `pong`'s
`palette_fx()`.

---

## 5b. The translucency trap — the Quabricks lesson

This one hid an entire port's slowness in plain sight, so it gets its own
section. Quabricks (a Tetris variant) ran well under 60 fps. We accelerated the
final present (the canvas→framebuffer copy) with the blitter — and the frame
rate **did not move at all**. The present copy was ~150k cycles; the real cost
was ~1.1M cycles *upstream*, and it was almost invisible in the source:

```c
// drawn EVERY frame, over a background that never changes:
fill_round_rect(board, ..., color_alpha(UI_BOARD_BG, 224)); //  24,000 px, BLENDED
fill_round_rect(panel_hold, ..., color_alpha(UI_PANEL, 18)); //  ~5,000 px, BLENDED
fill_round_rect(panel_stats, ...);                           // ~14,000 px, BLENDED
fill_round_rect(panel_next, ...);                            // ~20,000 px, BLENDED
draw_grid(alpha 16); draw_glow(alpha 40);                    // ~29,000 px, BLENDED
```

~95,000 blended pixels every frame. An opaque fill is a `memset` (~1 cyc/byte);
a **blended** fill runs a per-pixel `blend_px()` — src-over against the pixel
behind it, ~10–15 cyc/pixel even with the blend LUT warm. 95k × 12 ≈ **1.1M
cycles — over the entire 1.24M frame budget by itself.** No amount of present or
copy acceleration can touch that; the work is in the fill loop.

The insight that dissolves it: **every one of those translucent fills is over a
static background, so each blended result is a _constant color_.** `UI_PANEL` at
alpha 18 over the fixed backdrop is one specific RGB — compute it **once** and
draw it opaque (or bake it into a composited-once static frame; see §4/§6). That
converts ~90k blend-pixels into a handful of `memset`s that then don't even
recur, because static UI is drawn once, not per frame.

Rules this bought us:

- **Audit every `a<255` color that covers area.** A single translucent
  full-screen overlay is ≈ your whole frame budget. Translucency is the trap,
  not the copy.
- **A translucent fill over an unchanging background is a constant — bake it
  opaque.** Never re-blend the same card/backing/grid every frame.
- **Measure the draw, not the present.** If accelerating the present/flip
  changes nothing, the cost is upstream in how you *compose* the frame. Profile
  the fill loop, not the copy.
- **Full-screen dims/flashes (pause overlay, game-over fade, clear-flash) are
  `palette_set()`, not blended overlays** — the most expensive way to do the
  cheapest effect (§5).

---

## 6. Worked example — a Tetris/Quabricks-style game done right

The pathological version (what makes it crawl):

```c
// EVERY frame:
for (each cell of the well)                       // ~200 cells
    draw_rounded_block(screen, x, y, color);      // per-pixel + corner math, CPU
SDL_Flip(screen);                                 // + a second full-frame CPU copy
```

The accelerated version:

1. **Pre-render each block once** into a small `uint8_t` sprite (say 16×16),
   transparent index 0 in the corners. Do the rounded-corner math **once at
   startup**, not per frame.
2. **Keep a persistent frame buffer** you own (the well + HUD), not a
   throwaway shadow surface.
3. **Only redraw cells that changed.** A piece moving down dirties ~8 cells, not
   200. `blit_ck()` each dirty cell's sprite into your frame buffer.
4. **Present with the fast path:** `SDL_lite_present_indexed(frame, ...)` (Path
   B) or `fb_present_dma()` if you dropped the shim (Path C).
5. **Flash/clear-line effects** via `palette_set()`, not redraws.

Result: per frame you touch a few hundred bytes (the dirty cells) through the
blitter instead of ~150 KB through the CPU twice. That is the difference between
"needs optimization" and "runs at real speed."

Rule of thumb: **if your inner draw loop has `[y*w+x]` in it and runs every
frame over the whole screen, you are on the slow path.** Move the pixels into a
sprite built once, and let `blit_ck()` place them.

---

## 7. Coding-agent porting checklist (execute top-to-bottom)

When porting or optimizing an SDL/DOS game, apply in order and stop when the
frame budget (§1) is met:

- [ ] **Measure first.** Enable the HUD: `SDL_lite_stats(1)` overlays
      `M<ms> F<fps> A<audio-ms>`. If `M` > 16, you're over budget; note whether
      the cost is audio (`A`) or video.
- [ ] **Find the present.** Is the game calling `SDL_Flip`? That's Path A
      (slow). Does the game render into one contiguous indexed buffer it owns?
- [ ] **If yes → switch to Path B.** Replace `SDL_Flip(screen)` with
      `SDL_lite_present_indexed(buf, pitch, w, h, palette)`. This alone removes
      one full-frame CPU copy per frame. Verify pixels still land.
- [ ] **Measure the draw, not just the present.** If a present/copy optimization
      changes nothing, the cost is upstream in frame composition — profile the
      fill/draw loop. (This is how Quabricks' real bottleneck was found.)
- [ ] **Audit translucency.** Grep for `a<255` / `SDL_BLENDMODE_BLEND` /
      `color_alpha(...)` fills that cover area. A blended fill costs ~10–15
      cyc/pixel (§5b). If it's over a static background, bake the constant result
      and draw it opaque.
- [ ] **Kill per-frame full-screen redraws.** Anything static (background, HUD
      frame, unchanged board cells) must be drawn once and left. Track a dirty
      set; redraw only dirty regions. Drop redundant `RenderClear`/clears whose
      pixels a later full-screen fill overwrites anyway.
- [ ] **Pre-render sprites once.** Any per-frame procedural pixel math
      (gradients, rounded corners, shading) moves to a startup step that fills a
      sprite buffer. Reserve **index 0 = transparent**.
- [ ] **Composite with the blitter.** Replace CPU sprite `memcpy` loops with
      `blit()` (opaque) / `blit_ck()` (transparent). Honor the five rules in §4
      (alignment, flush source, present via `fb_present_dma`).
- [ ] **Overlap.** Kick `blit()` early, do logic, `blit_wait()` late.
- [ ] **Recolor via palette.** Convert fades/flashes/cycling to `palette_set()`.
- [ ] **Gate on capabilities.** Wrap every blit in
      `if (sys_caps()->features & HAL_FEAT_BLIT)` with a CPU fallback, so the
      binary still runs on a flavor without the engine.
- [ ] **Re-measure.** `M` should now be well under 16 ms.

### Reference implementations in this tree

- **Path B done right:** `sdk/sdl_lite.c` → `SDL_lite_present_indexed()`
  (the blit + `fb_present_dma` path). Tyrian drives this.
- **Raw blit API + correctness/speed proof:** `sdk/fbbench/main.c` — stages 6/7
  (`blit` + sustained blit-present) and stage 8 (`blit_ck` vs a CPU reference).
  This is the canonical usage: flush → kick → wait → present.
- **The slow path to avoid:** `sdk/sdl_lite.c` → `SDL_Flip()` (CPU shadow-copy).
  Fine for a prototype, wrong for a shipping game.
- **The translucency trap, real case:** Quabricks (`port/quabricks`) — clears +
  full-redraws every frame with ~95k blended pixels of *static* UI (§5b). The
  cautionary tale behind rules 4 and "measure the draw."

---

## 8. Quick reference

```c
// Fast SDL present (game owns one indexed frame buffer):
SDL_lite_present_indexed(frame, pitch, w, h, palette);   // Path B

// Raw blitter (Path C), the five-rule sequence:
if (sys_caps()->features & HAL_FEAT_BLIT) {
    flush_cpu_dcache_range(src, (h-1)*sstride + wbytes);
    blit(dst, src, wbytes, h, sstride, dstride);   // opaque, async
    blit_wait();
    fb_present_dma();                              // flip w/o full flush
}
blit_ck(dst, sprite, wbytes, h, sstride, dstride); // index 0 = transparent

// Free effects:
palette_set(rgb256);                               // recolor, no redraw

// Budget: ~1.24M cyc/frame @ 74.25 MHz. Blit full frame ≈ 0.1 ms; CPU copy ≈ 1.5 ms.
```

Feature bits: `HAL_FEAT_BLIT` (rect copy), `HAL_FEAT_BLITKEY` (colorkey-0),
`HAL_FEAT_PALETTE`. Always read them from `sys_caps()`; never assume.
