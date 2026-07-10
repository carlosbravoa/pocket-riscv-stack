# RISC-V Stack — a from-scratch game console for the Analogue Pocket

A soft RISC-V game console implemented on the Pocket's FPGA (Cyclone V
5CEBA4F23C8): CPU, video, audio, input and file loading built from scratch with
LiteX, exposed to games through a small C API. **Games are plain files on the
SD card** — writing one never touches the FPGA toolchain.

```
 Layer 3  GAME        your C code (sdk/pong is the worked example)
 Layer 2  HAL         hal.h — framebuffer, input, audio, files, timing
 Layer 1  BSP         crt0, generated csr.h accessors, liblitedram
 Layer 0  SoC         VexiiRiscv + LiteDRAM + video/audio/pak hardware
──────────────────────────────────────────────────────────────────────
          core_top.v  Analogue APF glue (bridge, scaler, i2s, slots)
```

## Hardware (what the console is)

| | |
|---|---|
| CPU | VexiiRiscv rv32im+zicbom @ 50 MHz, I/D caches |
| RAM | 64 MB SDR SDRAM (LiteDRAM) + 16 KB SRAM (stack) |
| Video | 320x240, 8bpp rgb332, double-buffered in DRAM, tear-free flip, 60 Hz |
| Audio | 48 kHz 16-bit stereo stream (FIFO + i2s) |
| Input | 2x Pocket controllers (buttons; analog reserved) |
| Storage | APF data slots: Game (the binary) + Pak (assets), pulled on demand |
| Boot | 32 KB ROM bootloader: pick game.bin from the Pocket menu → runs from DRAM |

Resource use: ~28% logic, ~35% BRAM — plenty of headroom (an OPL3 FM fork is
planned separately).

## Repository map

```
soc/pocket_soc.py       the SoC (LiteX): CPU, DRAM, video, audio, pak DMA, CSRs
soc/pocket_platform.py  module-level "pins" of the SoC
soc/hal/                hal.h + hal.c — the API games compile against
soc/firmware/           the ROM bootloader
soc/pocket_core/        Quartus project: core_top.v (APF glue) + build_core.sh
soc/spc_clone/out/      the Pocket package tree (core.json, artwork, Assets)
soc/tools/              artwork + demo-asset generators
sdk/                    game toolchain: game.ld, crt0, game.mk, examples, GUIDE.md
```

## Build the core (once)

Needs: Quartus 25.1std Lite, xpack riscv-none-elf-gcc, python venv with LiteX
(clone pins in DEPENDENCIES.txt), sbt+JRE (VexiiRiscv generation). Then:

```sh
. env.sh
cd soc && python pocket_soc.py --output-dir build/pocket        # elaborate
make -C firmware BUILD_DIR=$PWD/build/pocket                    # bootloader
python pocket_soc.py --firmware firmware/firmware.bin --output-dir build/pocket
cd pocket_core && VER=x.y.z ./build_core.sh                     # Quartus+package
```

`build_core.sh` syncs the generated RTL, compiles, bit-reverses, packages the
spc-clone tree and uploads the zip. Unzip at the SD card root.

## Write a game (the actual point)

```sh
cd sdk/pong && make            # -> pong.bin (~11 KB)
# copy to SD (e.g. Assets/riscv_stack/common/), pick in the core's Game slot
```

See **sdk/GUIDE.md** for the programming guide and `sdk/pong/main.c` for a
complete commented game (input, drawing, text, SFX, game states).

## Simulation

`cd soc && ./build.sh sim` boots the same firmware in Verilator with a UART
console (note: the sim runs forever — kill it when done). VCD tracing:
`--trace --trace-end <ps>` on pocket_soc.py.

## Status / history

Hardware-confirmed through v0.12.0 (bootloader console). Tags mark milestones;
`git log` tells the story: Stage 1 sim bring-up → APF hello → framebuffer →
SDRAM + double buffering → VexiiRiscv swap → audit (v0.7.0) → input → audio →
pak loading → 50 MHz → game-from-SD console (v0.12.0).
