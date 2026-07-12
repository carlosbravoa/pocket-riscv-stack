# DOOM on the riscv-stack — port notes

Shareware DOOM (doomgeneric) over `soc/hal/hal.h`, following the Tyrian
port's pattern. Milestone 1 (**verified on the PC twin**): boots to the
title, menus into E1M1, walks, fires, saves/loads within a session, SFX
mix into the 48 kHz stream, config persists across runs, quits cleanly.
**The console build has NOT been compiled or run yet** — see the
integration checklist at the bottom.

## Layout

    src/      vendored doomgeneric (GPL2, see ATTRIBUTION.md) — 3 small
              patches, each marked with an `RVSTACK:` comment in place
    compat/   the port seam (all riscv-stack-specific code lives here)
    assets/   doom1.wad lands here (`make wad`); packed by `make pak`

## Build & run (PC twin)

    cd sdk/doom
    make wad            # fetch shareware doom1.wad (freely distributable)
    make pak            # -> doom.pak (pakfs with doom1.wad inside)
    make doom-pc        # native build against desktop SDL2
    RVSTACK_PAK=doom.pak ./doom-pc

Keys (hal_pc's pad map): arrows move/turn, Z fire, X use, A(key) menu,
S = 'y' (confirm prompts), Enter = menu select, Tab automap, Q/W strafe,
ESC quits the twin. Headless smoke test:

    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy RVSTACK_PAK=doom.pak \
    RVSTACK_INPUT="40:X,80:T,120:T,160:T" \
    RVSTACK_SHOT="600:e1m1.bmp" ./doom-pc
    # X opens the menu (Doom's title only listens for ESC), three enters
    # start E1M1; the shot shows the hangar. SDL_AUDIODRIVER=disk with
    # SDL_DISKAUDIOFILE=out.raw captures the mixed SFX stream.

Console: `make` -> `doom.bin` (needs the SoC build tree, `BUILD_DIR=`
to point elsewhere); copy to SD, pick in the Game slot, `doom.pak` in
the Pak slot.

## What works (PC twin)

- **Video**: built with `-DCMAP256 -DDOOMGENERIC_RESX=320
  -DDOOMGENERIC_RESY=200` — the pipeline stays 8-bit palettized end to
  end. `DG_DrawFrame` (compat/dg_rvstack.c) letterboxes 320x200 onto the
  320x240 framebuffer and forwards PLAYPAL to `palette_set()`. No CPU
  pixel conversion anywhere.
- **Input**: pad edges -> Doom keycodes. D-pad=arrows, A=fire, B=use,
  Start=enter, Select=tab (automap), L1/R1=strafe, **X=escape (menu)**,
  **Y='y' (confirm)** — the last two are additions to the planned map:
  Doom's title screen only reacts to ESC, and quit/nightmare prompts
  demand a literal 'y', so the fixed map couldn't start or quit a game.
  Select+Start returns to the game picker.
- **Timing**: `sys_ticks_us()/1000`; wraps with the 32-bit us counter
  (~71 min) costing one hiccup — Doom only subtracts tick values.
- **Files**: `compat/stdio.h` (shadow header) routes ALL game stdio to
  `compat/rvfile.c`: reads try the mounted pakfs first (the WAD is a
  zero-copy window over pak DRAM through the stock `w_file_stdc.c`),
  everything else is a named session RAM file. The pak mounts at
  main_ram+0x2100000 (above game+save regions — the 3 MB default window
  is too small for the 4.2 MB WAD and sits below the game image; same
  spot Tyrian uses).
- **Config**: `default.cfg` persists through `save_open("config")` /
  `save_commit` — restored on boot, written on quit. 4 KB cap (the file
  is ~1.6 KB).
- **Savegames**: work within a session (RAM files; g_game's
  write-temp/remove/rename commit dance is implemented). To save on an
  empty slot from the pad: the name must be non-empty — press Y (types
  'y') then Start. **Not persisted across power-off** — see open items.
- **SFX**: `compat/i_rvsound.c` is doomgeneric's `DG_sound_module`: an
  8-channel mixer (DMX lumps converted to s16 once, cached on
  `driver_data`; nearest-neighbor resample; i_sdlsound's vol/sep gain
  map). `rvsound_pump()` runs once per frame from `DG_DrawFrame` and
  pushes 800 stereo frames to `audio_stream_write()` — the SDK pump
  model, no threads. Verified by capturing the stream (pistol shots
  present in the PCM).

## What's stubbed / open items

- **Music**: `DG_music_module` is a no-op. The right console
  implementation is a MUS -> OPL sequencer feeding `opl_write()` on the
  FM flavor (Doom's D_* lumps are MUS; GENMIDI has the instrument
  patches) — same shape as Tyrian's `compat/opl3_hw.c`. Gate on
  `sys_caps()->features & HAL_FEAT_FM`.
- **Savegame persistence**: one .dsg is ~25 KB at the E1M1 start and
  grows with level state; the HAL save window is 32 KB TOTAL per game
  (config included). Options for later: persist ONE slot (fits when
  small, fails politely when not), compress (savegames are highly
  RLE-able), or extend the save budget. For now `doomsav*.dsg` are
  session-only, silently.
- **Key remapping in config**: DEFAULT_KEY values >= 128 (doomgeneric's
  synthetic KEY_FIRE/KEY_USE codes) round-trip as literal keycodes (see
  the m_config.c RVSTACK patch); scancode-based configs from other ports
  still translate.
- **-nosound flag** works if SFX misbehave on hardware (add to argv in
  compat/dg_rvstack.c main()).

## Vendored-source patches (all marked `RVSTACK:` in place)

1. `src/m_config.c` — LoadDefaultCollection/SaveDefaultCollection bodies
   re-enabled (upstream ORIGCODE'd them out; the port persists config).
2. `src/m_config.c` — SetVariable keeps DEFAULT_KEY values >= 128
   literal instead of zeroing them (zeroing silently unbound fire/use
   after the first config round-trip — found the hard way).
3. `src/i_sound.c` — SDL_mixer include under FEATURE_SOUND removed
   (FEATURE_SOUND is served by compat/i_rvsound.c, not SDL_mixer).

## Console integration checklist (maintainer)

- [ ] `make` against the real SoC build tree — expect libc gaps first:
      `compat/libc_shim.c` (console-only, excluded from the PC build) is
      written by analogy with Tyrian's; symbols Doom needs beyond
      Tyrian's set are already added (strdup, strcasecmp/strncasecmp,
      strstr, atof, getenv=NULL, system=-1, vsscanf %s/%[set]) but the
      link is the judge.
- [ ] Binary size / heap: ~67k lines of C + a 6 MB zone (`i_system.c`
      DEFAULT_RAM) + ~2 MB SFX cache inside the 28 MB game region; the
      4.2 MB pak sits above it at +0x2100000 (see dg_rvstack.c).
- [ ] Frame rate: Doom renders whole frames on the CPU. At 66 MHz rv32im
      expect well under 35 fps in the hangar; the game logic tolerates
      any frame rate (TryRunTics), but if it crawls, first suspects are
      R_DrawColumn/R_DrawSpan memory patterns and the 320x200 memcpy in
      DG_DrawFrame (candidate for `blit()` on HAL_FEAT_BLIT flavors).
- [ ] Audio pacing: `rvsound_pump()` writes 800 frames/present. If video
      drops below 60 Hz the FIFO underruns (crackle) — acceptable first;
      fix by sizing the write to `audio_stream_free()`.
- [ ] Save hardware path: config persist uses the same
      save_open/save_commit sequence as Tyrian's cfg (proven), but
      verify `save_restore_code()` on first boot.
- [ ] The stats overlay: this port doesn't use sdl_lite, so
      `SDL_lite_stats` isn't available; profile with sys_ticks_us
      deltas + sys_diag, or a corner-pixel heartbeat.
- [ ] Upstream doomgeneric commit vendored: `dcb7a8db` (2026-04-12).
