# Prince of Persia (SDLPoP) — RISC-V Stack port

**Upstream:** `NagyD/SDLPoP` @ `3c5add5` (see `UPSTREAM_COMMIT`).
**Status:** IN PROGRESS — Stage 1 (scaffold).
**Effort class:** medium (~Wolf3D). Integer game logic is already done by the
decompilation; the work is the presentation + audio + file seam.

## Stage 0 — candidacy verdict: STRONG GREEN

Scored against `sdk/PORTABILITY.md`.

| Factor | Verdict |
|---|---|
| Language | C, no C++ |
| SDL | SDL2, minimal renderer (1 streaming texture + RenderCopy) → per-port SDL2 shim in `compat/` on shared `sdl_lite` (Wolf3D/Tyrian pattern; there is no shared `sdl2_lite` lib) |
| Game-logic math | **Integer** — `seg000`–`seg008` are the decompiled 8086 game. Only floats: a cosmetic HUD timer (`seg003.c:329`) and joystick `atan2` (`seg000.c:1308`) — both shimmed/removed |
| Threads | **None** |
| Input | SDLPoP has full `SDL_GameController` support already; map HAL pad onto it |
| Geometry | native 320×200, letterbox to 320×240 |
| Assets | ~4.3 MB of `.DAT` → pak (budget 31 MB) |
| Saves | small savestate, far under the 32 KB budget |

## The defining rework — presentation to 8bpp indexed

This SDLPoP composites **everything in 24-bit truecolor**
(`make_offscreen_buffer`, `seg009.c:~990` → 24bpp; sprites load 8bpp at
`seg009.c:854` but up-convert on blit). Our console is 8bpp + hardware palette.
Revert the compositor to **native 8bpp indexed**: offscreen surface at 8bpp,
index-preserving blits, drive `palette_set()` + `SDL_lite_present_indexed`.
Authentic *and* the fast path (ACCEL.md doctrine; Tyrian/Wolf3D pattern).

## Scope decisions (all standard)

- **Music → hardware OPL3.** `midi.c` drives a software Nuked OPL3 (`opl3.c`).
  Replace `opl3.c` with a `compat/opl3_hw.c` implementing the `opl3.h` API but
  forwarding register writes to `opl_write()` (gated on `HAL_FEAT_FM`) and
  returning silence — the Tyrian `compat/opl3_hw.c` pattern. Zero CPU synthesis.
- **Digitized SFX → PCM voices** (`pcm_play`).
- **DROP** `stb_vorbis.c` (Ogg music decode, 192 floats), `screenshot.c` (PNG
  writer), and the truecolor torch lighting (`lighting.c` / `USE_ALPHA` /
  `USE_LIGHTING` — per-pixel alpha over the frame, forbidden by ACCEL.md).

### config.h switches for the port
- OFF: `USE_LIGHTING`, `USE_COLORED_TORCHES`, `USE_SCREENSHOT`, `USE_ALPHA` (already off)
- KEEP: `USE_FADE`/`USE_FLASH` (palette-only, cheap), `USE_TEXT`/`USE_MENU`,
  `USE_QUICKSAVE`/`USE_REPLAY`, `USE_COPYPROT`

## Upstream object list (src/Makefile)
`main data seg000..seg009 seqtbl replay options lighting screenshot menu midi
opl3 stb_vorbis` — port KEEPS `main data seg000..seg009 seqtbl replay options
menu`; DROPS `stb_vorbis screenshot lighting opl3` (opl3 replaced by compat).

## Asset licensing — SHIP DECISION PENDING (not a port blocker)
The repo bundles PoP `.DAT` data (Mechner/Ubisoft copyright). Same call as
Tyrian (freeware) vs Wolf3D (shareware) — left to the maintainer; noted here so
it is not forgotten before any family-zip inclusion.

## Gate progress (Stage 2)
- [x] 1. **PC twin PLAYS — level 1 renders correctly** (verified headless:
       real PoP dungeon art — prince, torches, portcullis, brick walls — through
       the full seam). Screenshot proof captured via RVSTACK_SHOT.
- [ ] 2. Console links (picolibc gaps shimmed) — NEXT
- [ ] 3. Sim boots to gameplay (pak requested, boot beacons)
- [ ] 4. Hardware: render, music (FM), SFX, saves, pad map

## Data — RESOLVED (my earlier "blocked" call was WRONG)
The repo's `data/` tree IS the complete game: `data/<SET>/resNNN.png` (indexed
PNG) + .bin + .pal. Stock SDLPoP loads these loose files via its dir-fallback;
no packed `.DAT` needed. The port now:
- decodes indexed PNG palette-preserving via **lodepng** (compat/pop_png.c,
  compat/lodepng.c) — 1/4/8-bit expanded to 8bpp, palette kept so
  set_chtab_palette recoloring (dungeon/guards) works;
- serves loose files from a **path-keyed pak** (works on console AND twin):
  `make_pakfs --lower` stores the whole tree lowercased (`prince/res151.png`);
  rvfile looks up by normalized relative path (strip `data/`, lowercase);
  `rvfs_stat` reports pak path-prefixes as directories and pak-aware
  `file_exists` stops `locate_file` mangling paths — so `open_dat`'s dataset
  checks pass with no real filesystem. VERIFIED: level 1 renders from the pak
  ALONE (no disk). The PC twin keeps a real-fopen fallback for dev convenience.

Bugs fixed to get here (all in compat/pop_sdl.c): blit did a needless palette
deref on the 8bpp→8bpp index-copy path (hflip); **palettes weren't refcounted**
so freeing an hflip output (which shares its source's palette) corrupted the
source (use-after-free) — now SDL-style refcounted.

## Known gaps (post-gate-1)
- **Audio**: routed (MIDI→HW OPL3, digi→pump) but the HEADLESS dummy audio
  device doesn't drain, deadlocking the pump's backpressure. Real hardware/
  desktop drains fine. `RVSTACK_NOAUDIO=1` skips audio for headless render
  tests. Needs a real-audio (or Xvfb) run to verify sound.
- **fscanf configs declined**: SDLPoP.ini/mods + names.txt (Ogg) return NULL
  (our FILE* is fake; real fscanf spins on it). Game uses built-in defaults.
  TODO: shadow fscanf/fgets for full config/mod support.

## What is DONE (code-complete)
- Full `compat/` seam: `SDL.h` (SDL2 shadow, psdl_-renamed), `pop_sdl.c`
  (software surfaces/blit/events/timing + `pop_present` quantizer), bridge,
  `opl3_hw.c` (MIDI→HW OPL3), `rvfile.c` (pak+saves), `vorbis_stub.c`.
- `RVSTACK:` seam edits: `seg009.c` update_screen present + 48kHz audio;
  `types.h` SDL routing; `config.h` subsystem disables.
- Input: HAL pad → synthesized SDL2 controller/key events (process_events
  unchanged). SELECT+START = quit-to-picker.
- Fixed a latent shared-HAL bug: `hal_pc.c` didn't check `SDL_LockTexture`
  (crashed headless); guarded so RVSTACK_SHOT works without a display.
- Verified: `make pop-pc` clean; runs 8s, presents every frame, renders
  correct pixels through the seam (beacon visible; F7A/F7B telemetry).

## Render strategy (decided)
Keep PoP's truecolor compositor; reimplement the ONE present choke point.
Surfaces stay 24/32bpp (targets) + 8bpp (sprites, index-preserving). At present,
quantize the final 24bpp surface → 8bpp indices via a reverse-LUT against the
master `palette[256]`, upload `palette[256]` as the hardware palette, hand
indices to `SDL_lite_present_indexed`. Quantization cost is a Stage-3/4 profiler
item, not a gate-1 blocker.

## Seam implementation checklist — RESUME HERE
Done: scaffold, vendored src/@3c5add5, `Makefile`, `compat/SDL.h` (shadow header,
psdl_-prefixed funcs; enums/scancodes match real SDL2).

Remaining compat/ files (drive `make pop-pc` to compile, then run = gate 1):
- [ ] `compat/pop_sdl.c` — implement every `psdl_*` from SDL.h: software surface
  create/free (8/24/32bpp), `BlitSurface` (8bpp-src→truecolor-dst palette
  lookup + colorkey + alphamod/blend), `FillRect`, `MapRGB/A`, `SetPaletteColors`,
  `ConvertSurfaceFormat`, `Lock/Unlock` (no-op), clip. Event queue
  (`PollEvent`/`PushEvent` ring). Timing (`GetTicks`/`Delay`/`GetPerformance*`
  via `sys_ticks_us`). Audio open/pause store the callback. Cursor/hint/msgbox
  no-ops.
- [ ] `compat/rv_bridge.h` + `compat/lite_bridge.c` — sole includer of
  `sdl_lite.h`: `rvb_present_indexed(idx,pal256)`, `rvb_audio_open/pump`,
  `rvb_poll` (HAL pad → controller/key state). pad→PoP-control map here.
- [ ] `compat/opl3_hw.c` — implement `opl3.h` API (`OPL3_Reset`,
  `OPL3_WriteReg[Buffered]`, `OPL3_Generate[Stream]`): forward reg writes to
  `opl_write()` (FM-gated, NEW-mode `0x30` on `0xC0` regs), generate SILENCE.
  Drops the software synth; midi.c's timing/counting is untouched.
- [ ] `compat/pop_snd.c` — audio bring-up: register PoP's `audio_callback`
  (seg009) with `rvb_audio_open`; pump feeds `audio_stream_write`. Set
  `digi_samplerate = 48000` so the callback outputs our native rate directly.
- [ ] `compat/stdio.h` + `compat/rvfile.c` — shadow stdio → pak reads + HAL
  saves (adapt Wolf3D's rvfile.c; set `upersist[]` = config + 1 savegame).
- [ ] `compat/libc_shim.c`, `compat/math_shim.c` — console-only picolibc/libm
  fill-ins (check gamelib first; never redefine what it provides).

Seam edits in vendored src/ (all tagged `RVSTACK:`):
- [ ] `seg009.c` `update_screen()` (~2765) + `get_final_surface()` (~2669) →
  quantize+present (the choke point).
- [ ] `seg009.c` `set_gr_mode` (~2559), `init_scaling` (~2505) → drop
  window/renderer/texture/hint bring-up; keep `onscreen_surface_`/`offscreen`.
- [ ] `seg009.c` `process_events()` (~3427) → fill `key_states[]`,
  `last_key_scancode`, `joy_button_states[]`, `joy_axis[]` from `rvb_poll`.
- [ ] `seg009.c` `init_digi` (~2155) → `digi_samplerate = 48000`, route
  `audio_callback` through pop_snd; drop IMG_Load/Haptic/RWops.
- [ ] `midi.c` `opl_write_reg` (~336) — hooked via opl3_hw.c (no edit needed if
  OPL3_WriteReg is the choke; confirm).
- [ ] `seg000.c` file access via `open_dat`/`load_from_opendats_*` → pak (mostly
  handled by shadow stdio; confirm `locate_file`/dir paths).
- [ ] `config.h` — OFF: `USE_LIGHTING`, `USE_COLORED_TORCHES`, `USE_SCREENSHOT`;
  ensure `USE_ALPHA` off. Add `-DPOP_RVSTACK` guard for our edits.
- [ ] `main.c` — SELECT+START quit-to-picker (`save_commit`+`sys_exit`); no atexit.

Key seam facts (from the SDLPoP map):
- Present choke: `update_screen()`/`get_final_surface()` (seg009 ~2669-2792).
- OPL choke: `midi.c` `opl_write_reg`→`OPL3_WriteReg` (opl3.c:1277).
- Audio: `audio_callback` (seg009 ~2085) S16 stereo; `init_digi` `SDL_OpenAudio`
  (~2184); `digi_samplerate` (~1889).
- Files: `open_dat` (seg000 ~438), `load_from_opendats_alloc` (seg009 ~2958).
- Input: `process_events` (seg009 ~3427); controls applied seg000 ~336-343.
- Entry: `main`→`pop_main` (seg000:38)→`init_game_main`→`start_game`.

## Pad map
TBD (Stage 3). SELECT+START = quit-to-picker (save_commit then sys_exit).

## Deviations from upstream
Every local change in `src/` is marked `RVSTACK:` so `git diff` of `src/` IS the
port. Presentation/audio/file seam lives in `compat/`.
