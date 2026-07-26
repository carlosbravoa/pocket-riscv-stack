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
- [x] 2. **Console LINKS** — `pop.bin` builds (entry 0x40400000, 376 KB).
       picolibc-minimal gaps filled in compat/libc_shim.c + math_shim.c
       (printf/scanf family, ctype, strtol, qsort, perror, getenv, stat/mkdir
       stubs), rv32 setjmp/longjmp, dirent + setjmp shadow headers, and 5
       int64<->float helpers compiler_rt lacks. lodepng compiles on rv32.
- [~] 3. **Boots on real RTL** — pak streams + mounts, init runs (beacons
       entry→options→video), no crashes after the argv fix. BLOCKED on
       PERFORMANCE: runtime lodepng PNG decode ~1.17M cyc/glyph (255 font
       images ~= 300M cyc; thousands of sprites) — too slow at 74 MHz.
       Fixed on the way: find_exe_dir g_argv[0] NULL-deref (argc=0/argv=NULL,
       PORTABILITY.md #7). Trap handler now emits mepc/mtval/ra.
- [x] 3b. **Assets pre-converted + fast graphics load** (DONE): PNG tree -> raw
       indexed blobs (tools/png2raw.py, "RVI1"; pop_png reads via memcpy, no
       lodepng); pakfs find() is now bsearch on a sorted directory; locate_file
       short-circuits on console. RTL boot: font 93M->5.6M cyc, beacons 1..5
       fast, no traps. `make pop.pak` runs the converter.
- [x] 3c. Sound loading FIXED: convert_digi_sound resampled with float (soft-
       float on rv32 = tens of M cyc/sound); rewrote as fixed-point 16.16
       (POP_RVSTACK). All 57 sounds now load in 22.8M cyc.
- [x] 3. **BOOTS TO GAMEPLAY ON RTL** — beacons 1..7 (entry→pak→video→PRINCE→
       sprites→sounds→level 1), renders frames (F7A/F7B telemetry), ran the
       full 400M-cycle scenario with NO traps. Fixed en route: one
       Load-misaligned trap in play_seq's SEQ_JMP (misaligned *(word*) read of
       the seqtbl jump target -> byte-wise, like the existing __PSP__ path).
- [x] 4. **RUNS ON HARDWARE WITH SOUND** (2026-07-23). First run found three
       bugs, all fixed — see "Gate 4" below. The killer was a one-line
       `__bswapsi2` infinite recursion; with it fixed the title sequence
       completes, the 16-px vertical bars are gone and MUSIC PLAYS. Long-run
       soak (deeper levels, save/load, menus) still outstanding.
       NOTE: gameplay frame time ~83 ms (~12 fps) in sim (F7A=0x346) — the
       per-frame truecolor->8bpp quantize (64000 px reverse-LUT) is the likely
       cost; profile + optimize the present path (ACCEL.md) as Stage-3 polish.

## Gate 4 — first hardware run (2026-07-23, RiscvStackFM)

Reported: (1) every character on the first text screen was a solid white box;
(2) the title picture faded in, then "vertical bars" appeared and it froze
hard; (3) no audio at any point. (2) and (3) turned out to be the SAME bug.

### Bug 1 — text renders as solid boxes (FIXED, verified on the twin)
`seg009.c method_3_blit_mono()` (the font renderer) converts a colorkeyed 8bpp
glyph to ARGB8888, then overwrites the RGB of **every** pixel with the text
colour and relies on **per-pixel alpha alone** to mask the background.
`compat/pop_sdl.c psdl_BlitSurface()` only honoured the surface-wide `alphamod`
and ignored per-pixel alpha, so the whole glyph cell was painted → a box.
Fixed by folding `src_alpha * alphamod` per pixel (SDL2 BLENDMODE_BLEND
semantics) and skipping fully transparent pixels.

Two latent colour-model bugs found and fixed alongside it — both would have
corrupted *coloured* text and rects once the boxes were gone:
- 32bpp surfaces stored bytes as `R,G,B,A`, but `SDL_MapRGB` returns
  `0x00RRGGBB` and the game pokes 32bpp surfaces through a `uint32_t*`
  (`method_3_blit_mono`, `draw_rect_contours`) → R/B swapped. Storage is now
  true ARGB8888 (`B,G,R,A` on LE), matching MapRGB/MapRGBA.
- The 24bpp `Rmask` described the wrong byte, so `seg009.c RGB24_bug_check()`
  concluded our `FillRect` swaps R/B and `safe_SDL_FillRect` "corrected" a swap
  that never happened → every `method_5_rect()` came out R/B-swapped. Masks now
  describe the actual byte order, and the probe correctly reports "not affected".

### Bug 2 — "vertical bars + total freeze" = `__bswapsi2` infinite recursion (FIXED)
**Root cause, one line in `compat/libc_shim.c`:**
```c
unsigned __bswapsi2(unsigned x) { return __builtin_bswap32(x); }   /* WRONG */
```
rv32im has no byte-swap instruction (that is Zbb), so GCC lowers
`__builtin_bswap32()` to a LIBCALL — to `__bswapsi2` — i.e. the function calls
itself unconditionally:
```
__bswapsi2:  addi sp,sp,-16 ; sw ra,12(sp) ; jal __bswapsi2
```
Almost certainly reached from `midi.c parse_midi()` (MIDI headers are
big-endian), which is why it fired the instant the title music started AND why
there was never any audio.

**Every symptom follows from that one line:**
- Each recursion writes ONE WORD EVERY 16 BYTES (`sw ra,12(sp)` after
  `sp -= 16`) while marching down through memory. Crossing the framebuffer, that
  is corrupted pixels at a **16-byte stride** — the "vertical bars at an exact
  16-pixel pitch". They looked like a scanout/DRAM artifact; they were a
  runaway stack.
- sp then leaves valid memory and a store faults → `rvstack_trap` → but the
  handler's OWN first stack store re-faults on the ruined sp → it ping-pongs
  into mtvec forever. Hence **no red bars and no 0xDEAD diag**: the reporter
  died before it could report. Silent, total freeze (no present, no input, no
  audio), deterministic, identical on base and FM.

**How it was finally found** (after several wrong turns — see below): the RTL
committed-PC histogram. `soc/sim/tb_core_top.cpp` `RVSTACK_POPHANG=1` watches
the 0xB1A6xxxx title-stage diags and, when the diag port goes quiet, samples
`WhiteboxerPlugin_logic_commits_ports_0_pc`. Result was unambiguous: 82% of
commits on `rvstack_trap`'s first instruction, the rest split evenly across the
three instructions of the recursion. **When a hardware freeze defeats
inspection, go straight to this — it names the spin loop in one run.**

**Fixes:**
1. `__bswapsi2` implemented with explicit shifts/masks. The input is `volatile`
   so GCC's bswap-idiom pass cannot recognise the pattern and re-emit a call to
   this very function. Verify after any change: `objdump --disassemble=__bswapsi2`
   must show shifts and a plain `ret`, never a `jal` to itself.
2. `sdk/gamelib.c`: `rvstack_trap` is now `naked` and switches to a dedicated
   1 KB `trap_stack` before touching memory. A handler that spills onto the
   FAULTING stack is useless exactly when it matters most. Benefits every game.
3. `blit_wait()` (hal.c) and `SDL_lite_audio_pump()` (sdl_lite.c) are now
   bounded. Neither was this bug, but an unbounded spin on a hardware status
   bit turns any stall into an unrecoverable freeze. Each emits a diag if its
   cap fires (0xB11D blitter, 0xB11E pump).

**Wrong turns worth not repeating** (all inferred from twin screenshots before a
hardware photo existed, each cost a hardware test cycle):
- "It is `transition_ltr()` crawling" — no. A twin shot happened to catch the
  transition mid-sweep. The `overshoot > 9` clamp made for it is harmless and
  still in (it does fix a real degeneration above ~92 ms/frame) but is unrelated.
- "FM SDRAM fragility" — no. Carlos had already seen it on base; it reproduced
  identically on both, which should have killed this immediately.
- "The quantizer" — no. The SELECT+L1 flat-fill test (still in the build) proved
  the bars survive a solid-colour fill, i.e. they come from below `pop_present`.
- A stage marker that does NOT force a present cannot be read from a photo of a
  wedged screen — the count silently lags. `rvstack_stage_show()` presents.

### Bug 3 — no audio: frame time starves the FIFO
The music engine is fine — `RVSTACK_OPLLOG` on the twin shows a healthy OPL3
stream (proper init, 278 key-on events) for the intro theme. The problem is
pacing: `SDL_lite_audio_pump()` runs **once per present**, and the FIFO is only
~42 ms deep. At ~90 ms/frame the FIFO runs dry for roughly half of every frame,
so music arrives as fragments. Frame time IS audio quality here. Mitigated by
the quantizer speed-up + the transition fix; re-check on hardware.

`pop_present()`'s inner loop now run-length memoises the previous pixel, which
skips the cache-hostile random access into the 64 KB reverse-LUT for the long
flat runs that dominate PoP's art. Verified pixel-identical on the twin (the
only differing pixels were the HUD's own frame-time digits).

### Twin gap closed: audio is now testable headlessly
`sdk/pc/hal_pc.c` derived the FIFO level from the **host** queue, so the pump's
"top up until full" loop misbehaved on every headless sink: `dummy`/`disk` never
drain (pump blocks forever) and a null ALSA PCM drains instantly (the loop never
terminates — the game never presents another frame). That is the deadlock noted
under "Known gaps", and it made every audio-dependent path untestable. The twin
now models the console FIFO on the wall clock (fixed depth, 48 kHz drain), so
`make pop-pc` runs the full title sequence with audio enabled and no device.

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
