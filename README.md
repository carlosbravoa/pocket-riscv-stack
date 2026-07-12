# RISC-V Stack — a from-scratch game console for the Analogue Pocket

A soft game console implemented on the Pocket's FPGA (Cyclone V 5CEBA4F23C8):
CPU, video, audio, input, file loading and saves built from scratch with LiteX,
exposed to games through a small C API. **Games are plain files on the SD
card** — writing one never touches the FPGA toolchain.

**The proof: OpenTyrian2000 ships in the family zip and is playable** —
scrolling parallax backgrounds, hardware-blitted presents, per-game saves that
survive a power cycle, and on the FM flavor the soundtrack plays on a **native
OPL3 synthesizer in fabric** (Greg Taylor's opl3_fpga): the game's classic
AdLib register writes go to real silicon; the CPU never synthesizes a sample.

**The family:** one platform, one SDK, one set of game binaries — several core
flavors that coexist on the same SD card and share `Assets/riscv_stack/common/`:

| Core | Branch | Adds | Menu name |
|---|---|---|---|
| Base console | `main` | — | RiscvStack |
| FM synth | `opl3` | hardware OPL3 (AdLib/OPL2 program compatible) | RiscvStackFM |

A game asks the hardware what it offers (`sys_caps()->features`) and adapts —
the same `pong.bin`/`tyrian.bin` run on every flavor (Tyrian's music plays on
the FM core's real OPL3 and is silent on the base core, by detection).

```
 Layer 3  GAME        your C code (sdk/pong is the worked example)
 Layer 2  PORTLIB     pakfs (files-in-a-pak) + sdl_lite (SDL-1.2 subset)
 Layer 2  HAL         hal.h — framebuffer, input, audio, files, saves, timing
 Layer 1  BSP         crt0, generated csr.h accessors, liblitedram, gamelib
 Layer 0  SoC         VexiiRiscv + LiteDRAM + video/audio/pak hardware
──────────────────────────────────────────────────────────────────────
          core_top.v  Analogue APF glue (bridge, pak FSM, save window, i2s)
```

## Hardware (what the console is)

| | |
|---|---|
| CPU | VexiiRiscv rv32im+zicbom @ 66 MHz, I/D caches |
| RAM | 64 MB SDR SDRAM (LiteDRAM); 27 MB game region + 1 MB stack |
| Video | 320x240, 8bpp palettized (256-entry hardware palette), double-buffered in DRAM, tear-free deferred flip, 60 Hz; rect-copy DMA blitter |
| Audio | 48 kHz 16-bit stereo stream + 4 mixed PCM voices; hardware OPL3 on FM flavors |
| Input | 2x Pocket controllers (buttons + analog) |
| Storage | APF data slots: Game (the binary), Pak (assets, up to 16 MB), Save |
| Saves | per-game `<game>.sav`, host-managed (nonvolatile slot, SNES-style), created on demand |
| Boot | 32 KB ROM bootloader: pick game.bin from the Pocket menu → runs from DRAM |

## Repository map

```
soc/pocket_soc.py       the SoC (LiteX): CPU, DRAM, video, audio, pak DMA, CSRs
soc/pocket_platform.py  module-level "pins" of the SoC
soc/hal/                hal.h + hal.c — the API games compile against
soc/firmware/           the ROM bootloader (game picker)
soc/pocket_core/        Quartus project: core_top.v (APF glue) + build_core.sh
soc/sim/                FULL-SYSTEM SIMULATION: real core_top + real SoC +
                        scripted Pocket host (Verilator) — see below
soc/spc_clone/out/      the Pocket package tree (core.json, artwork, Assets)
soc/tools/              make_pakfs.py (pak packer), family zip, artwork
sdk/                    game toolchain: game.ld, crt0, game.mk, gamelib
sdk/pakfs.*, sdl_lite.* portlib: pak filesystem + SDL-1.2 shim for ports
sdk/pc/                 PC TWIN: the whole hal.h on desktop SDL2 — build any
                        game natively (make -f ../pc/pc.mk) and iterate fast
sdk/pong                the worked example game (input, palette FX, SFX, saves)
sdk/tyrian              OpenTyrian2000 port (GPL game; SDK stays BSD)
```

## Write a game (the actual point)

```sh
cd sdk/pong && make                  # -> pong.bin (~15 KB)
# copy to SD (Assets/riscv_stack/common/), pick in the core's Game slot
make -f ../pc/pc.mk && ./pong-pc     # same game, native on your desktop
```

See **sdk/GUIDE.md** for the programming guide (framebuffer, input, audio,
saves, porting real games with portlib).

## Build the core (once)

Needs: Quartus 25.1std Lite, xpack riscv-none-elf-gcc, python venv with LiteX
(clone pins in DEPENDENCIES.txt), sbt+JRE (VexiiRiscv generation). Then:

```sh
. env.sh
cd soc && python pocket_soc.py --output-dir build/pocket        # elaborate
make -C firmware BUILD_DIR=$PWD/build/pocket                    # bootloader
python pocket_soc.py --firmware firmware/firmware.bin --output-dir build/pocket
cd pocket_core && VER=x.y.z ./build_core.sh                     # Quartus+package
cd .. && VER=x.y.z ./tools/make_family_zip.sh                   # THE artifact
```

Only the FAMILY zip is distributed (both flavors, one install: unzip at the
SD card root). Per-flavor zips are build intermediates.

## Verify before hardware (the workflow that keeps releases honest)

Three layers, cheapest first:

1. **PC twin** (`sdk/pc/`) — game logic at desktop speed under a debugger.
2. **Full-system simulation** (`soc/sim/run_sim.sh`) — the REAL core_top RTL
   and the REAL SoC boot the REAL bootloader and a REAL game under Verilator,
   with a scripted Pocket host serving the APF bridge protocol. Scenarios:
   save round-trips across reboots (`GAME=savetest`), portlib
   (`GAME=pakfstest`), FM synthesis on the opl3 branch (`GAME=fmtest --fm`).
   This rig root-caused every hardware bug of the v0.17.x series.
3. **Hardware** — for what only silicon can tell you.

## Status / history

Hardware-confirmed: video, input, PCM audio, hardware FM (OPL3), game
loading/exit/re-pick, per-game save file creation. Tags mark milestones;
`git log` tells the story: Stage-1 sim bring-up → framebuffer → SDRAM →
VexiiRiscv → game-from-SD console (v0.12) → family + FM (v0.15-16) →
the great save saga + full-system sim (v0.17.x) → portlib + PC twin +
OpenTyrian → blitter + 66 MHz + profile-driven optimization → v0.19.7:
Tyrian playable, saves power-cycle verified, native OPL3 soundtrack (the
released milestone). Roadmap: FM retrigger fix → sprite-layer perf →
1.0 ABI freeze.

## Standing on the shoulders of giants

This console was "built from scratch" only in the sense that a house is built
from scratch on land someone else surveyed, with tools someone else forged.
Every layer below our C code exists because someone did years of hard work in
the open and gave it away. In three days we went from nothing to a playable
Tyrian with hardware FM — those were not our three days; they were decades of
other people's, composed. If this project helps you build yours, that's the
chain continuing. Thank these people, in this order:

- **[LiteX](https://github.com/enjoy-digital/litex)** (Florent Kermarrec /
  Enjoy-Digital) — the SoC builder this entire console stands on: bus fabric,
  CSRs, **LiteDRAM** (our SDRAM controller and every DMA port the video,
  blitter and pak loader use), BIOS scaffolding, and the discipline of a
  generated, inspectable SoC.
- **[VexiiRiscv](https://github.com/SpinalHDL/VexiiRiscv)** (Charles Papon /
  Dolu1990, SpinalHDL) — the RISC-V CPU. Its WhiteboxerPlugin commit port is
  also why our simulator doubles as a statistical profiler.
- **[opl3_fpga](https://github.com/gtaylormb/opl3_fpga)** (Greg Taylor) — a
  YMF262 in SystemVerilog. The reason a 1995 shooter's soundtrack plays on
  real synthesis silicon instead of a CPU emulator. LGPL, honored in
  `soc/pocket_core/opl3/`.
- **[Verilator](https://verilator.org)** (Wilson Snyder et al.) — the
  simulator behind `soc/sim/`. Every hardware bug this project root-caused —
  the save saga, the stack overflow, the FIFO drops — fell to this rig first.
  If you take one method from this repo: simulate the WHOLE system, not units.
- **[OpenTyrian / OpenTyrian2000](https://github.com/opentyrian/opentyrian)**
  — the meticulous C port of Tyrian that made the game portable at all, and
  **Jason Emery / Eclipse Software** for making Tyrian's assets freeware.
- **[agg23](https://github.com/agg23)** (openFPGA examples, sound_i2s, and
  hard-won APF findings — including documenting that bridge `openfile` writes
  are broken in Pocket firmware, which saved our save system) and the authors
  of the open-source Pocket cores whose `data.json` taught us the canonical
  nonvolatile save mechanism.
- **[Analogue](https://www.analogue.co/developer)** — openFPGA: a shipping
  handheld that lets you replace its heart with your own hardware.
- **[picolibc](https://github.com/picolibc/picolibc)** (Keith Packard) — the
  libc under every game binary, and **the xPack GNU RISC-V toolchain** that
  compiles it all.

If you are building something like this: start from these projects, read
`sdk/GUIDE.md` and `soc/sim/` for how we wired them together, and expect the
same lesson we kept relearning — when the hardware misbehaves, the fastest
path is almost never the device; it's a simulator honest enough to reproduce
the bug and an instrument precise enough to see it.
