# Commander Keen 4 (Omnispeak) on the RISC-V Stack — porting notes

Omnispeak (github.com/sulix/omnispeak, GPL-2.0, vendored pristine in
`src/`) built as Keen 4 only, over the shareware v1.4 EGA data. **Status:
E1 level 1 plays end-to-end on the PC twin** (render, scroll, HUD, input,
OPL music stream verified via RVSTACK_OPLLOG) **and `keen.bin` links with
the real RISC-V toolchain**. Sim + hardware are the maintainer's gates;
boot beacons are already wired.

## Stage-0 candidacy verdict (why this port was a GO)

- C99, single-threaded game code — zero `float`/`double` in game logic
  (checked; the only hits were comments), zero thread use outside the
  SDL backends we don't compile. Integer/fixed-point throughout.
- 8-bit palettized 320x200 EGA (16 colors) — inside the 320x240 envelope.
- **Perfect seam**: Omnispeak has pluggable backend tables
  (`VL_Backend`/`IN_Backend`/`SD_Backend`) with a reference software-PAL8
  null backend, plus a self-contained Filesystem Manager (`id_fs.h`).
  We implement those four seams hal.h-DIRECT — **no sdl_lite/sdl2_lite**
  (noted choice: extending sdl2_lite would have added a layer for nothing;
  this is the Doom pattern applied to Omnispeak's own abstraction).
- Audio is AdLib register programs (IMF-style music + register-list SFX)
  → exactly what `opl_write` wants. PC speaker is the only loss.
- Assets: shareware Keen 4 v1.4 EGA is freely distributable, ~1.1 MB
  total with Omnispeak's extraction files — trivially inside the pak
  budget. Saves are small (see budget below).
- Upstream already parses files byte-wise/`memcpy`-wise (it ships on DOS
  and ARM), so alignment trap #3 is largely pre-paid.

## Build & run (PC twin)

```sh
cd sdk/keen
make pak       # data/ -> keen.pak (~1.9 MB)
make keen-pc
RVSTACK_PAK=./keen.pak ./keen-pc
# music/FM register stream (PC twin has no OPL3 — log it):
RVSTACK_PAK=./keen.pak RVSTACK_FORCE_FM=1 RVSTACK_OPLLOG=opl.txt ./keen-pc
# headless proof-of-life (warp straight into level 1):
SDL_VIDEODRIVER=dummy RVSTACK_PAK=./keen.pak \
  RVSTACK_SHOT="500:lvl.bmp" RVSTACK_INPUT="250:LEFT" ./keen-pc /TEDLEVEL 1
```

Console: `make` (needs `soc/build/pocket`; the game.mk include is
BUILD_DIR-guarded so pak/PC builds work without the toolchain).

## What works (verified on the PC twin, headless RVSTACK_SHOT/INPUT)

- Intro/terminator credits scroller, menus, in-level gameplay (E1L1 via
  /TEDLEVEL 1 and via the demo loop): tile scroll, sprites, score box,
  EGA palette, letterboxed 320x200. Injected pad input moves Keen.
- OPL music+SFX stream: 4268 writes in ~25 s of E1L1 incl. 1364 key-ons;
  NEW-mode init (105=01, 104=00) and **all** 0xC0 writes carrying the
  L/R enable bits (the OPL2-program-on-OPL3 trap). On non-FM flavors
  `opl_write` is a hardware no-op — one binary, any flavor, silent music.
- Files: Keen + Omnispeak data from the pak (case-folded, zero-copy);
  config (OMNISPK.CFG, CONFIG.CK4) and savegames as named RAM files with
  a persisted subset (below).

## Pad map (documented per the platform convention)

d-pad=arrows (move/menus), A=LCtrl (jump), B=LAlt (pogo), X=Space
(shoot), Y=`Y` (yes prompts), R1=`N` (no prompts), L1=F1 (help),
START=Enter (confirm/status), SELECT=Esc (menu/back).
**SELECT+START held = flush saves + exit to the game picker.**
No joystick is reported; the pad IS the DOS keyboard (Keen's stock
`in_kbd_*` defaults), so every menu works untouched.

## Saves — the 32 KB window budget

| region       | cap    | contents                                    |
|--------------|--------|---------------------------------------------|
| omnispk_cfg  | 2 KB   | OMNISPK.CFG (text config)                    |
| config       | 2 KB   | CONFIG.CK4 (vanilla config + high scores)    |
| savegam0     | 24 KB  | SAVEGAM0.CK4 (slot 0)                        |

Each region stores `[magic][u32 len][data]`. A Keen 4 save = game state
(~64 B) + 3 RLEW-compressed map planes + ~70 B/object — typically
5–12 KB, so slot 0 fits with margin. Slots 1–5 exist as session-RAM
files (work within a run, vanish at power-off) — same policy as the
Wolf3D port. `FS_CloseFile` on a written file commits immediately;
quit paths flush everything.

## What's stubbed / deviations

- **320x200 letterboxed** on 320x240 (mode 0xD is baked into Keen);
  the 20-px bands are painted in the EGA border color.
- **PC speaker**: silent stub (`sdm_PC` selectable but inaudible).
- **Timing**: no timer thread/interrupt — `RVK_TimerPump()` converts
  `sys_ticks_us` into 140/560 Hz `SDL_t0Service` calls from present /
  pumpEvents / every wait loop. One marked source edit (`VL_GetTics`
  yields into the backend) prevents the upstream busy-wait deadlock.
- **Text input** (high-score name entry): no keyboard — START (Enter)
  accepts an empty name.
- src edits (all marked `RVSTACK:`): VL_GetTics yield; per-episode
  `#ifdef` guards in ck_play.c (upstream bug: unguarded CK5/CK6 symbol
  refs); dead dirent-using helpers in id_ca.c compiled out via
  `-DRVSTACK`.
- Open items: quiet-SFX menu untested; demo playback drifts were not
  audited; music tempo on real silicon should be compared against the
  PC-twin OPLLOG.

## Architecture map

```
src/*                    vendored Omnispeak (edits marked RVSTACK:)
compat/rv_keen.h         seam contract + beacon/pump/quit decls
compat/id_vl_rvstack.c   VL_Backend: PAL8 surfaces, present->fb_backbuffer,
                         palette_set after fb_present (glitch-free fades),
                         waitVBLs pumps timer + fb_flip_poll
compat/id_in_rvstack.c   IN_Backend: pad bits -> IN_HandleKeyDown/Up edges,
                         SELECT+START quit convention
compat/id_sd_rvstack.c   SD_Backend: alOut -> opl_write (0xC0|=0x30, NEW
                         mode), PIT-accurate RVK_TimerPump, HAL_FEAT_FM gate
compat/id_fs_rvstack.c   FS manager: pak reads + RAM user files + save_open/
                         save_commit persistence; remove() for slot overwrite
compat/libc_shim.c       console-only picolibc gap fillers (checked against
                         libc.a AND gamelib: no malloc/mem*/rand duplicates;
                         adds v/snprintf, sscanf, strtol, ctype, getenv,
                         fprintf wrapper, exit->flush+sys_exit)
```

## Console checklist (sim + hardware, the maintainer's gates)

1. `keen.bin` (267 KB) links clean against soc/build/pocket — done here.
2. Sim: `SKIP_SOC=1 GAME=keen ./run_sim.sh` (add a keen pak case);
   watch `0xBEAC0001..4` beacons: 1=pak+saves up, 2=video mode,
   3=SD up, 4=first present. `0xDEAD____` = trap + mcause.
3. Memory: MM uses plain malloc (gamelib heap); EGAGRAPH cache is a few
   MB — fine in 27 MB. Largest compat locals are ~1 KB (stack-safe).
4. FM flavor: compare music against opl.txt; non-FM: confirm silence.
5. Saves on hardware: save in-game to slot 0, power-cycle, load;
   `save_restore_code()` diagnostics if it doesn't come back.
6. Perf at 66 MHz: full-surface present memcpy is ~64 KB/frame — if the
   frame rate sags, move the present copy to `blit()` (noted follow-up).
