# SkyRoads — RISC-V Stack port (Skyroads-c)

**Source of truth:** Skyroads-c (`~/devel/fpga/games/Skyroads-c`, MIT) — a
faithful C++17 reimplementation of the 1993 Bluemoon DOS game: real TREKDAT
rasterizer, the EXE's 5-substep ship physics, MUZAX AdLib driver, all
equivalence-tested against the original data files. See `UPSTREAM_COMMIT`.

This REPLACES the earlier prototype in this directory (2026-07-24), which was
a simplified from-scratch C rewrite specced off the SkyRoads-Codex Rust
project — invented renderer, single-step physics, no audio. Nothing of it
remains; the pak name, beacon scheme and sim scenario carry over.

## Stage 0 verdict (2026-07-27)

GREEN: complete tested reimplementation; game core is platform-clean (SDL2
confined to one host file, replaced here); assets 668 KB of freeware, pak'd;
36 Hz physics tick; deterministic double math stays deterministic under
soft-float (IEEE-exact). C++17 with exceptions (58 throws in the data
loaders) — the OpenJazz lane: libsupc++, `.eh_frame` kept, `rvstack_eh_init()`.

The two porting work items, both with established patterns:
1. Renderer output was RGBA with per-pixel palette math baked in; the DOS
   original renders 8-bit indexed and animates the DAC — exactly our
   fb + CLUT model. Re-plumbed upstream (branch `rvstack-indexed`) to
   index + 256-entry palette output, proven pixel-exact by expanding
   index+palette→RGBA through the upstream `frame_hash` equivalence tests.
2. Music was ymfm (software YM3812, per-sample floats — a non-starter on
   rv32im): the OPL register stream goes to the real OPL3 instead
   (`compat/opl_chip_hw.cpp`), sequencer time-driven at 180.02 Hz
   (`MuzaxPlayer::advance`). Silent on non-FM flavors, house policy.

## Layout

| Piece | Where |
|---|---|
| Vendored upstream (data/core/renderer/audio) | `src/` — edits tagged `RVSTACK:` |
| hal-direct host (36 Hz loop, input latch, present, saves) | `compat/host.cpp` |
| pak-backed assets (upstream's `*_bytes` loaders) | `compat/pak_assets.cpp` |
| Console audio (MUZAX→OPL3, SFX→`pcm_play`→vmx voices) | `compat/audio_rv.cpp` |
| OplChip on hardware | `compat/opl_chip_hw.cpp` |
| Freestanding C++ runtime | `compat/cxx_rt.cpp` (from OpenJazz) |

NOT vendored: `src/platform` (SDL2 host), `src/cli`, `src/audio/opl_chip.cpp`
+ `third_party/ymfm`, the tests (they run upstream, where the indexed
renderer is proven).

## Console conventions

- 320x200 letterboxed on 320x240 (rows 20..219); palette_set right after
  fb_present (the DOS DAC-fade model, glitch-free per the hal.h contract).
- Pad: dpad = menus / throttle+brake / steer, A = jump + skip intro,
  B = back (Esc), START = select / restart, SELECT+START = quit to picker
  (progress persisted first).
- Progress: the original's own 66-byte `skyroads.cfg` block (checksum word,
  2 setting words, 30 road-completion words) via `save_open("skyroads.cfg")`;
  persisted on leaving gameplay and at quit. Budget: 66 B of the 32 KB window.
- Pak: `skyroads.pak` (data/, lowercased). Manual pick wins; otherwise
  auto-bound by name (`pak_bind_named` → `/Assets/riscv_stack/common/`).
- Boot beacons (`sys_diag`): 0xBEAC0001 entry, ..2 pak mounted, ..3 levels
  parsed, ..4 assets+renderer+audio ready, ..7 first frame. Sim scenario:
  `RVSTACK_SKYROADS=1`.

## Gate progress

- [x] 1. PC twin plays — headless RVSTACK_INPUT/RVSTACK_SHOT run: intro
       fade-in, title menu, GoMenu world grid, gameplay on Red Heat 1 with
       backdrop/blocks/ship/dashboard all faithful. (Caught here: hal-direct
       hosts MUST call `input_poll()` once per loop — `input_buttons()` only
       returns the latch, and hal_pc's headless input injection also hangs
       off the poll.)
- [x] 2. Console links — skyroads.bin ~315 KB (see Makefile notes: newlib
       C++ headers + `-lstdc++`, int32_t aligned to `int`, libc/rv shims).
- [x] 3. Boots in RTL sim — beacons 1→2→3→4→7, first frame @313.8M cycles
       (~4.2 s on hardware; ~3.3 s of that is the LZS parse of all archives
       at beacon 3→4). `GAME=skyroads ./run_sim.sh` PASSED, 0 failures.
- [ ] 4. Hardware

## Open / expected flags

- Ship sim is double-precision soft-float (deliberately models DOS
  fixed-point; IEEE-exact so upstream's equivalence tests still vouch for
  console behavior). ~thousands of ops per 36 Hz tick — expected fine;
  profile at gate 3, fixed-point conversion is the fallback.
- `render_scene` returns the framebuffer by value (a 64 KB heap alloc per
  frame through gamelib malloc). Accepted for bring-up; make it a persistent
  buffer if the profile complains.
- libstdc++ vs picolibc: std::string/vector out-of-line symbols
  (`__throw_length_error` & co) may need `-lstdc++` or tiny shims at gate 2.
- Debug view modes (Tab on desktop) are not mapped on the pad.
