# Wolfenstein 3D on the RISC-V Stack — porting notes

Wolf4SDL v2.0 (Chaos-Software fork, SDL2 dialect) on the riscv-stack SDK,
following the OpenTyrian2000 port's architecture. **Milestone 1 status:
shareware E1L1 plays end-to-end on the PC twin** — render, input, files
from the pak, digitized SFX, and the IMF music engine emitting a verified
OPL3 register stream. The console build is written but not yet run on
hardware (no RISC-V toolchain on the dev machine — see the checklist).

## Build & run (PC twin)

```sh
cd sdk/wolf3d
make pak            # data/*.wl1 (shareware v1.4, committed) -> wolf3d.pak
make wolf3d-pc
RVSTACK_PAK=./wolf3d.pak ./wolf3d-pc              # keyboard = the pad:
                                                  # arrows, Z=A(fire) X=B(open)
                                                  # A=X(strafe) S=Y(run)
                                                  # TAB=SELECT(menu) RETURN=START
# music/FM path (PC twin has no OPL3 — log the register stream instead):
RVSTACK_PAK=./wolf3d.pak RVSTACK_FORCE_FM=1 RVSTACK_OPLLOG=opl.txt ./wolf3d-pc
# headless proof-of-life (SDL dummy drivers):
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy RVSTACK_PAK=./wolf3d.pak \
  RVSTACK_SHOT="150:e1l1.bmp" RVSTACK_INPUT="120:A" \
  ./wolf3d-pc --tedlevel 0 --normal --nowait
```

`--tedlevel N` warps straight into a level (0 = E1L1); `--nowait` skips
the press-a-key/title waits (useful headless, where nobody can press one).

## What works (verified on the PC twin)

- **Video**: fixed 320x200x8, letterboxed on the 320x240 framebuffer.
  Signon, PG13/title/credits, menus, and in-game rendering (walls,
  sprites, weapon, status bar) all present through
  `SDL_lite_present_indexed` (zero-copy; palette rides every present, so
  fades work). FizzleFade presents its in-progress frames.
- **Input**: pad -> SDL2 scancodes -> Wolf's stock key bindings:
  d-pad=arrows (move/menus), A=LCtrl (fire), B=Space (open/use),
  X=LAlt (strafe), Y=RShift (run), R1=P (pause), SELECT=Esc (menu
  open/back), START=Return (confirm). Verified: fire drops ammo in-game;
  SELECT opens the in-game Options menu.
- **Files**: every game `fopen` is rerouted at compile time
  (compat/stdio.h shadow) — reads come from the pakfs archive as
  zero-copy DRAM windows, writes (config, savegames) go to named RAM
  files. Same seam on both targets; the PC twin also reads only the pak.
- **Digitized SFX**: the VSWAP 7042 Hz pages are mixed straight from the
  PM page cache in the audio callback (16.16 stepper to 48 kHz, 8
  channels, 2 reserved for player/boss weapons, stereo panning with the
  same volume curve as Mix_SetPanning, `channelSoundPos` positioning
  serviced by wl_game.c as upstream).
- **Music + AdLib SFX — the showpiece**: `compat/id_sd_rv.c` replaces
  id_sd.c entirely. Wolf3D music is IMF (timestamped OPL register dumps)
  and AdLib SFX are OPL register programs; both are forwarded to
  `opl_write()` — on the FM flavor the REAL OPL3 does the synthesis,
  zero DSP on the CPU. The 700 Hz sequencer is clocked by counting
  samples inside the pumped audio callback (the original
  SDL_IMFMusicPlayer's scheme; sample-accurate, no threads, no timers).
  Verified via `RVSTACK_OPLLOG`: NEW-mode init (105=01, 104=00), correct
  instrument/key-on traffic for "Get Them!", and every 0xC0 write
  carrying the L/R output-enable bits (OPL2-program-on-OPL3 trap).
  On non-FM flavors `opl_write` is a hardware no-op: music silently off,
  digitized SFX carry the game — one binary, any flavor.

## What's stubbed / deviations

- **PC-speaker mode**: `sdm_PC`/`sds_PC` silently promote to AdLib —
  there is no beeper. (Menus still list the modes; selecting PC gives
  AdLib.)
- **Mouse & joystick**: stubs returning "absent"/zero deltas. The pad IS
  the input; SDL joystick enumeration reports none.
- **SDL_SaveBMP** (debug screenshots), window grab, message boxes:
  stubbed (message boxes print to the console/UART).
- **Read This!** works; text pages come from the vgagraph chunks.
- **Upstream bug fixed**: wl_def.h's `itoa`/`ltoa` sized their snprintf
  by `strlen()` of an *uninitialized* buffer (UB — HUD showed "1" for
  100 % health). Now plain sprintf.
- Base is the SDL2-dialect Wolf4SDL v2.0 the task named — the compat
  seam is therefore SDL2-shaped, which is exactly the shape of Tyrian's
  proven compat layer (OpenTyrian2000 is SDL2 too), so the two ports
  share architecture file-for-file.

## Asset pipeline

`data/` holds the freely-distributable shareware v1.4 episode
(audiohed/audiot/gamemaps/maphead/vgadict/vgagraph/vgahead/vswap
`.wl1`, lowercase, + vendor.doc). `make pak` packs it with
`soc/tools/make_pakfs.py` into `wolf3d.pak` (~1.2 MB — comfortably
inside the HAL's default 3 MB pak window; no tyrian-style relocation
needed). Dropping the registered `.wl6` files into `data/` instead
should work unchanged (`CheckForEpisodes` probes the pak for
`vswap.wl6` first), but only shareware has been tested.

## Saves — the 32 KB window layout

All regions share the Pocket's 32 KB per-game save window (TOC
included). Persisted slots (see `upersist[]` in compat/rvfile.c; each
costs cap+4 bytes staged):

| region        | cap      | contents                                   |
|---------------|----------|--------------------------------------------|
| config.wl1/6  | 1.5 KB   | 0xfefa magic, high scores, sound modes, bindings, viewsize (~0.5 KB actual) |
| savegam0.wl1/6| 26 KB    | one savegame slot                          |

A Wolf3D save is tilemap (4 KB) + actorat (8 KB) + areaconnect (1.3 KB)
+ live objects (~70 B each) + statobjlist + doors ≈ **20-28 KB** — so
exactly ONE slot fits the window. Slots 1-9 exist as session-RAM files
(save/load works within a run; they vanish at power-off). Fitting more
slots means compression (tilemap/actorat RLE-compress extremely well) —
noted follow-up. Savegame binary layout is arch-dependent (upstream
writes raw structs incl. pointer-relative state fields), so PC-twin
saves don't transfer to the console; config does not have that problem
in practice but is also not intended to transfer.

## Architecture map (who talks to what)

```
src/*                 vendored Wolf4SDL v2.0 (edits marked // RVSTACK)
compat/SDL.h          SDL2 names over the seam; EVERYTHING implemented in
                      compat is wsdl_-renamed on BOTH targets (trap #1:
                      real-SDL2 link hijack on the PC twin / sdl_lite
                      collision on the console)
compat/wsdl.c         surfaces (with format->palette), SDL2 event queue,
                      mouse/window stubs
compat/lite_bridge.c  the ONLY file including sdl_lite.h: present path,
                      pad keymap, audio open, load-progress beacons
compat/id_sd_rv.c     SD_* sound manager on hal.h (opl_write + callback
                      mixer); includes hal.h FIRST and #undef's alOut
                      (trap #2: include order)
compat/stdio.h        shadow header: fopen/fread/... -> compat/rvfile.c
compat/rvfile.c       pakfs reads (zero-copy) + RAM writes + HAL save
                      persistence; unlink/remove supported (F5 overwrite)
compat/libc_shim.c    console only: picolibc-minimal gap-fillers
compat/math_shim.c    console only: sin/cos/atan2/sqrt for BuildTables
```

## Console integration checklist (next session, with the toolchain)

1. `make` in sdk/wolf3d (needs `soc/build/.../variables.mak`; the
   include is guarded so PC-twin builds don't require it). Expect the
   usual picolibc noise; libc_shim/math_shim carry what Tyrian needed —
   Wolf may want 1-2 more symbols (watch for link errors; `log2` in
   SD_Startup is already gone with id_sd.c).
2. Wolf allocates ~2 MB via malloc at startup (PM page cache + vgagraph
   cache + signon) — gamelib's heap handles it; watch `_end` vs the
   28 MB region.
3. Stack: wl_draw/wl_scale recursion is shallow; largest locals are the
   menu's `SDL_Color pal[256]` (1 KB) — fine for the 16 KB console stack
   (limits doc says 1 MB budget; either way no >64 KB locals were added).
4. First light: beacons 1-6 paint during InitGame (photo tells the dead
   stage); `sys_diag` 0xBEAC000n mirrors them for the sim testbench.
5. Timing sanity at 66 MHz: the raycaster at 320x200 was comfortable on
   a 486; watch the SDL_lite stats HUD (top-right, enabled by default —
   compile out with -DWOLF_NOHUD).
6. FM flavor: confirm music on real silicon (same register stream the
   OPLLOG showed). Non-FM flavor: confirm silence + digi SFX.
7. Savegame-on-hardware: verify savegam0 persist/restore across power
   cycles; then decide on compression for more slots.
8. SELECT+START to exit to the picker comes free from sdl_lite; Wolf's
   own Quit menu item also lands in sys_exit via libc_shim's exit().

## Verification artifacts (reproduce with the headless commands above)

- Signon, main menu, E1L1 gameplay screenshots via RVSTACK_SHOT.
- HUD: FLOOR 1 / SCORE 0 / LIVES 3 / HEALTH 100% / AMMO 8, ammo 8->7
  after injected fire.
- opllog: 2014 writes in ~35 s of E1L1 incl. 438 key-ons — the IMF
  player is emitting a sane stream paced by the audio callback.
