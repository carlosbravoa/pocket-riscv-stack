# The Portable Class — what runs well on RISC-V Stack

The SDK targets a *class* of games, not any one title. A candidate that fits
the class ports in days (Tyrian, Doom, Wolf3D, sdl2-tetris did); one that
doesn't will fight the platform. Judge candidates against this spec **before**
starting.

## Hardware envelope

| Resource | Budget | Notes |
|---|---|---|
| CPU | rv32im @ 66 MHz, in-order, **no FPU** | fixed-point/integer games only; soft-float exists but is for cold paths |
| RAM | 27 MB heap + 1 MB stack | generous; but **>64 KB stack frames are a landmine** (Tyrian's loadMap hang) |
| Video | 320x240, 8-bit palettized, 60 Hz | 256-color hardware palette; use the FULL 240 lines when the game allows (Pocket displays it pixel-perfect at 5x) |
| Blit | hardware rect-copy DMA | via `blit()`; the present path uses it automatically |
| Audio | 48 kHz s16 stereo FIFO (~42 ms), 4 PCM voices, **OPL3 on FM flavor** | pump model — NO threads, NO callbacks-from-interrupts |
| Input | 2 gamepads (dpad+ABXY+shoulders+start/select) | no keyboard/mouse yet (docked keyboard planned) |
| Assets | pak file (read-only), up to ~31 MB, ~1 MB/s load | `make_pakfs.py`; big paks cost boot seconds |
| Saves | 32 KB per game, host-persisted | survives power-off; plan savegame size EARLY (Doom's 25 KB/slot barely fits one) |

## The green flags (port will go well)

- C (C++ is possible but fights the toolchain), SDL 1.2 or simple SDL2
- 8-bit palettized rendering at 320x200/240 (DOS-era, or flat-color 2D)
- Integer or fixed-point math throughout
- Single-threaded main loop, audio via a fill-me callback
- Assets in loose files or archives you can repack into a pak
- Small persistent state (config + scores + a savegame)

## The red flags (think twice)

- Float-heavy inner loops (Quake-class 3D, physics engines)
- Truecolor rendering that can't quantize to 256 colors
- Hard thread dependencies (worker threads, async loaders)
- Networking, mouse-driven UI, resolutions above 320x240
- Assets that can't legally ship or exceed ~31 MB

## Platform traps (every one of these cost us a debugging session)

1. **Link namespace**: any symbol named like a real SDL function hijacks
   desktop libSDL2 in the PC twin. Use the `RVL_`/`RVSDL2_` macro scheme
   (see `sdl_lite.h`, `sdl2_lite.h`, tyrian's `compat/`).
2. **Include order**: `hal.h` before any header that `#define`s conflicting
   names (Tyrian's `opl.h` vs `opl_write`); `#undef` after.
3. **Alignment**: VexiiRiscv **traps on misaligned word access**. x86 (the PC
   twin) silently tolerates it — casting byte buffers to `uint32_t*`/struct
   pointers is the #1 hardware-only crash. Parse file formats byte-wise.
4. **Stack frames**: keep locals under a few KB; big scratch buffers go
   static or on the heap.
5. **picolibc-minimal gaps** (gamelib/shims provide some): no allocator
   (gamelib has malloc), no `rand/srand` (gamelib), no `memcmp` (gamelib),
   byte-loop `mem*` (gamelib overrides, word-wide), **no `v*printf` at all**,
   no libm (`fabs/tan/atan2/...` — shim what you need), no `strdup`, no
   `sys/uio.h`; its `stdlib.h` *declares* `itoa/ltoa` (rename yours).
6. **Audio is pull-paced**: generate `audio_stream_free()` frames, never a
   fixed amount per video frame (Doom shipped with 800/frame at 35 fps —
   chronic underrun). Never block inside a delay loop.
7. **main() gets argc=0, argv=NULL** (crt0 guarantees it) — don't rely on
   args; use compile-time defaults.
8. **OPL3 timing**: `opl_write` handles CDC pacing and the fast-retrigger
   guard; don't add your own delays, and gate music on
   `sys_caps()->features & HAL_FEAT_FM`.

## The workflow (in order, no skipping)

1. **PC twin first** (`sdk/pc/`): the whole port runs on desktop SDL2.
   Debug logic, files, input, audio here. Env instruments: `RVSTACK_PAK`,
   `RVSTACK_SHOT` (frame dumps), `RVSTACK_INPUT` (scripted pads),
   `RVSTACK_OPLLOG` (timestamped OPL writes), `RVSTACK_FORCE_FM`.
2. **Console link early**: `make` against the SoC build tree the moment it
   compiles — libc gaps surface at link time, not on hardware.
3. **Full-system sim** (`soc/sim/run_sim.sh`): boot the real bin on the real
   RTL. Traps print `mcause` on the diag port; red bars on hardware = the
   same trap. Add `rvb_progress`-style boot beacons before hardware.
4. **Hardware last** — for what only silicon can tell you.

Worked examples, most instructive first: `sdk/tyrian` (the full pattern),
`sdk/doom` (hal.h-direct seam), `sdk/wolf3d` (OPL showpiece + shadow stdio),
`sdk/tetris` (sdl2_lite), `sdk/midiplay` (HAL-native app).
