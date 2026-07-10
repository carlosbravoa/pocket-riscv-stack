# RISC-V Stack SDK — write Pocket games in plain C

The FPGA core is a fixed **console**: a VexiiRiscv CPU at 50 MHz, 64 MB RAM, a
320x240 double-buffered framebuffer, 48 kHz stereo audio, controller input and
file loading — all behind one small API (`soc/hal/hal.h`). Games are ordinary
files on the SD card; **iterating a game never touches Quartus**.

## Write a game

```c
#include "hal.h"

int main(void) {
    sys_init();                       // adopts the running console state
    for (;;) {
        input_poll();
        uint8_t *fb = fb_backbuffer();    // 320x240, 1 byte/px rgb332
        // ... draw ...
        fb_present();                     // tear-free flip, paces to 60 Hz
    }
}
```

The full API (video, input, audio stream, pak files, timing) is documented in
`soc/hal/hal.h`. The demo game in `demo/` exercises all of it.

## Build & run

```sh
cd sdk/demo && make            # -> demo.bin
# copy demo.bin to the SD card (e.g. Assets/riscv_stack/common/)
# Pocket: open the RISC-V Stack core -> Game slot -> pick demo.bin
```

The bootloader (in the core's ROM) pulls the binary into DRAM and jumps to it.
Re-picking a different game from the Pocket menu restarts the flow.

## Memory map a game sees

| Range | What |
|---|---|
| `0x40400000` + 28 MB | your game (code+data+bss, linked by `game.ld`); heap after `_end` |
| `0x40100000` + 3 MB | asset pak area (`pak_open` pulls the Pak-slot file here) |
| `0x40000000` + 256 KB | framebuffer pages — via `fb_backbuffer()` only |
| `0x10000000` + 16 KB | on-chip SRAM: your stack |

Assets: ship a data file, have the user pick it in the **Pak** slot, then
`pak_open()/pak_read()/pak_seek()` it. Pad pak files by >= 2 bytes (the loader
never pulls the final 2 bytes — APF quirk; `game.mk` pads game binaries
automatically).

## v1 caveats

- Builds against this repo's SoC build tree (`soc/build/pocket`) — run
  `./build.sh` there once first. A relocatable SDK export is future work.
- Fixed rgb332 color (palette support planned), 48 kHz-only audio stream,
  no interrupts (poll everything), games don't return (power off / re-pick).
