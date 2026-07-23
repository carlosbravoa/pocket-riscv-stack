# Prince of Persia (SDLPoP) port — handoff summary

Status snapshot for picking this back up later. Detail lives in `PORTING.md`;
this is the "where is it, how does it work, what's next" overview.

Upstream: `NagyD/SDLPoP` @ `3c5add5` (see `UPSTREAM_COMMIT`). Engine is
GPL-3.0-or-later; the SDK/port glue is BSD-2-Clause.

---

## Status: gates 1–3 DONE, gate 4 (hardware) is next

| Gate | State | Evidence |
|---|---|---|
| 1. PC twin plays | ✅ | Level 1 renders on desktop from the raw pak alone (screenshots) |
| 2. Console links | ✅ | `pop.bin` builds for rv32 (entry 0x40400000) |
| 3. Boots to gameplay on RTL | ✅ | Full Verilator sim: beacons 1–7, renders frames, ran 400M cycles no traps |
| 4. Hardware | ⏳ | Not started — needs the physical Pocket |

**Bottom line:** Prince of Persia boots and plays through level 1 on the
silicon-equivalent RTL simulation. Every hard problem (render architecture,
asset pipeline, sound, alignment) is solved and committed. What remains is
real-hardware bring-up + a frame-rate optimization.

---

## How the port works (the load-bearing decisions)

- **No shared `sdl2_lite`.** Like Wolf3D/Tyrian, PoP carries its own SDL2-shaped
  shim in `compat/` on top of the shared `sdl_lite` (SDL-1.2 primitives) + the
  HAL. Every implemented SDL fn is `psdl_`-renamed so the PC twin's real SDL2 is
  never hijacked.
- **Render = keep PoP's truecolor compositor, quantize at the ONE present
  choke.** PoP composites in 24bpp; `seg009.c update_screen` (RVSTACK:) is
  reimplemented to quantize the final surface → 8bpp indices (reverse-LUT vs the
  master palette) → hardware palette + blitter present (`pop_sdl.c pop_present`).
  This is the ~12 fps cost noted under "further testing".
- **Assets are pre-decoded on the host** (`tools/png2raw.py`): the loose PoP PNG
  tree → tiny raw indexed blobs (`RVI1` magic), same filenames. `compat/pop_png.c`
  reads them with a memcpy — ZERO runtime PNG decode (lodepng was ~1.17M
  cyc/glyph on rv32; it stays only as a PC-twin loose-dir fallback).
- **Pak file layer** (`compat/rvfile.c` + shadow `compat/stdio.h`): `fopen`/
  `fread`/`fstat`/`stat` are routed to the pak (path-keyed, lowercased) or HAL
  saves. `make_pakfs --lower` sorts entries so `pakfs find()` binary-searches.
  `locate_file` short-circuits on console (no real FS) — this was a ~17× asset-
  load speedup.
- **Audio → hardware OPL3.** `compat/opl3_hw.c` replaces Nuked OPL3: `midi.c`'s
  register writes forward to `opl_write()` (FM-gated), zero CPU synthesis.
  Digitized SFX resample via a fixed-point 16.16 loop (`convert_digi_sound`,
  RVSTACK: — the float version was soft-float-bound). Ogg dropped.
- **Input:** HAL pad → synthesized SDL2 controller/key events, so PoP's
  `process_events` is unchanged. SELECT+START = quit to picker.
- **Every edit to vendored `src/` is tagged `RVSTACK:`** → `git diff` of `src/`
  IS the port.

---

## Tooling & commands (from `sdk/pop/`)

Always `. ../../env.sh` first (venv + riscv-none-elf-gcc + Quartus).

```sh
# ---- PC twin (fast dev loop; desktop SDL2) ----
make pop-pc
# headless run against the pak (no display needed):
ln -s /home/carlos/devel/fpga/SDLPoP/data data          # loose data (PC fallback)
make pop.pak                                             # or use the pak alone
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy RVSTACK_NOAUDIO=1 \
  RVSTACK_PAK=pop.pak RVSTACK_INPUT="40:T,150:T,300:A" \
  RVSTACK_SHOT="200:/tmp/f.bmp" ./pop-pc                 # dumps frame 200 as BMP
#   RVSTACK_SHOT="N:file"  dump frame N   RVSTACK_INPUT="frame:BTN"  inject pad
#   RVSTACK_NOAUDIO=1      skip audio (headless dummy audio deadlocks the pump)
#   RVSTACK_OPLLOG=<file>  dump the OPL register stream PoP emits (music check)

# ---- console binary + pak ----
cp -r /home/carlos/devel/fpga/SDLPoP/data data
make pop.pak                                             # runs png2raw then make_pakfs
make BUILD_DIR="$PWD/../../soc/build/pocket"             # -> pop.bin (rv32 flat binary)

# ---- full-system RTL sim (real SoC + CPU + bridge) ----
cd ../../soc/sim
SKIP_SOC=1 GAME=pop RVSTACK_POP=1 ./run_sim.sh > /tmp/sim.log 2>&1
#   RVSTACK_POP=1  the pop scenario: serves the pak, injects START/A past the
#                  splash, prints "[TB] beacon N @cycle", catches 0xDEAD traps.
#   drop SKIP_SOC=1 only if the SoC/firmware changed (slow re-elaborate).
```

### Boot beacons (watch these in the sim / on hardware)
`0xBEAC000n` on the diag port, painted as a colored bar on screen:
1 pop_main entry · 2 global options · 3 video/font · 4 PRINCE opened ·
5 entering load_all_sounds · 6 sprites+sounds loaded · 7 start_game (level 1).

### Trap debugging (the workflow that found every bug)
On any CPU trap, `gamelib.c rvstack_trap` now emits 4 diags:
`0xDEAD00xx` (xx = mcause) then **mepc**, **mtval** (faulting addr), **ra**
(caller). Map them to functions:
```sh
riscv-none-elf-nm -n sdk/pop/pop.elf > /tmp/syms.txt
# then binary-search /tmp/syms.txt for the address (see git history for the snippet)
riscv-none-elf-objdump -d sdk/pop/pop.elf --start-address=<mepc-16> --stop-address=<mepc+16>
```
mcause 4/6 = misaligned load/store → a `*(word*)`/`*(u16*)` cast on a byte
buffer (PORTABILITY.md #3); fix with byte-wise reads or `sdk/unaligned.h`.

---

## Further testing (gate 4 + polish)

### 1. Hardware bring-up (the main remaining work)
No reflash needed — the `.bin` runs on the existing bitstream.
- Copy `pop.bin` (Game slot) + `pop.pak` (Pak slot) to the SD (e.g.
  `Assets/riscv_stack/common/`).
- On the FM flavor (`RiscvStackFM`), verify: render, **OPL music** (PoP MIDI →
  `opl_write` → AdLib), digitized SFX, saves (`quicksave.sav`), the pad map,
  SELECT+START quit.
- Red bars on screen = a CPU trap the sim didn't hit; photograph the last beacon
  bar to see which stage, then reproduce in sim.
- **Audio verify BEFORE hardware:** `RVSTACK_OPLLOG` on the PC twin dumps the
  exact OPL register stream — compare against a known-good AdLib capture.

### 2. Frame-rate optimization (Stage-3 polish)
Gameplay was ~83 ms/frame (~12 fps) in sim. The suspect is `pop_present`'s
per-frame 64000-pixel truecolor→8bpp reverse-LUT quantize. Options: profile with
the RTL committed-PC histogram (see the tyrian `RVSTACK_PROFILE` scenario), then
either speed the quantizer (smaller LUT / skip unchanged rows) or move more
compositing to the blitter (ACCEL.md). This is polish, not a blocker.

### 3. Likely more alignment traps deeper in gameplay
Only level-1 kid animation was exercised. Other seqtbl paths, guards, level
transitions, menus, and save/load may hit more misaligned `*(word*)` reads. Each
is a 5-minute fix now: run the sim, read mepc/mtval, byte-wise the cast. Grep
`src/` for `*(word*)`/`*(short*)`/`*(dword*)` casts as a proactive sweep.

### 4. Asset shipping decision (not a port blocker)
The repo bundles PoP data (Mechner/Ubisoft copyright). Same call as Tyrian
(freeware) vs Wolf3D (shareware) — decide before any family-zip inclusion.

### 5. Config/mods (minor)
`SDLPoP.ini`/mod configs are declined on console (our fake `FILE*` can't do real
`fscanf`); the game uses built-in defaults. To support them, shadow
`fscanf`/`fgets` in `compat/`.

---

## Key files

```
sdk/pop/
  PORTING.md            full gate ledger + design notes
  HANDOFF.md            this file
  Makefile              console + PC-twin + pak targets
  tools/png2raw.py      host PNG -> raw indexed (RVI1) converter
  compat/
    SDL.h               SDL2 shadow header (psdl_-renamed)
    pop_sdl.c           software surfaces/blit/events/timing + pop_present (quantize)
    pop_png.c           RVI1 raw loader (+ lodepng fallback) + RWops
    opl3_hw.c           MIDI -> hardware OPL3
    rvfile.c stdio.h    pak-backed FILE + saves; rvfs_stat/fileno; path lookup
    lite_bridge.c       the only includer of sdl_lite.h (present + audio + beacons)
    libc_shim.c math_shim.c   console libc/libm fill-ins + rv32 setjmp/longjmp
    dirent.h setjmp.h   shadow headers
    vorbis_stub.c       Ogg dropped
  src/                  vendored SDLPoP; RVSTACK:-tagged edits ARE the port

Shared SDK changes this port made (used by all games):
  sdk/gamelib.c         rvstack_trap now emits mepc/mtval/ra
  sdk/pakfs.c           find() binary-search
  soc/tools/make_pakfs.py   --lower + sort-by-final-key
  soc/sim/tb_core_top.cpp   RVSTACK_POP scenario
  sdk/pc/hal_pc.c       SDL_LockTexture guard (headless render)
```

Recent commits: `git log --oneline | grep sdk/pop` (gate 1 → gate 3).
