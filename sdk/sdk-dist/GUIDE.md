# Writing games for the RISC-V Stack — the guide

Everything a game can do goes through one header: `soc/hal/hal.h`. This guide
covers the API, the patterns the hardware rewards, and how `sdk/pong` uses them.

## The frame loop

The display runs at 60 Hz and `fb_present()` blocks until the scanout hands you
the next hidden page — the loop below is automatically paced to the display:

```c
#include "hal.h"

int main(void) {
    sys_init();                    // adopts the running console state
    audio_stream_open(48000);      // if you make sound

    for (;;) {
        input_poll();              // snapshot the pads for this frame
        // update();               // game logic
        // draw();                 // into fb_backbuffer()
        // audio_frame();          // push 800 samples (one frame of sound)
        fb_present();              // tear-free flip, waits for the display
    }
}
```

Budget: at 50 MHz you have ~833,000 CPU cycles per frame. A full-screen
software redraw (76,800 pixels) is comfortably within it — pong + full clear +
text uses well under half.

## Video

- `fb_backbuffer()` → `uint8_t*` to a hidden 320x240 page, **1 byte per pixel,
  rgb332** (`RRRGGGBB`). Get it fresh every frame — pages alternate.
- `fb_present()` publishes your page. Never draw after present in the same frame.
- Write-only access patterns are fastest (the buffer is cached; the HAL handles
  the cache/DMA coherency on present). Prefer 32-bit stores for fills — see the
  plaid loop in `sdk/demo/main.c`.
- Text: include `font8x8_basic.h` (in `sdk/`) and blit glyph bits — `pong`'s
  `text()`/`center()` are ~20 lines, copy them.
- Useful colors (default palette): `0xFF` white, `0xE0` red, `0x1C` green,
  `0x03` blue, `0x1F` cyan, `0xFC` yellow.
- **Palette**: the framebuffer byte is an index into a 256-entry RGB888
  hardware palette. It boots as the rgb332 mapping above, and
  `palette_set(rgb[256][3])` replaces it — fades, flashes and color cycling
  cost one palette reload instead of a frame redraw. Reload right after
  `fb_present()` for glitch-free effects; see `pong`'s `palette_fx()`.

## Input

```c
input_poll();                          // once per frame
uint32_t b = input_buttons(0);         // player 1 bitmap
if (b & HAL_BTN_LEFT) ...              // held
uint32_t edge = b & ~prev; prev = b;   // pressed THIS frame (do this yourself)
```

Bitmap: `HAL_BTN_UP/DOWN/LEFT/RIGHT/A/B/X/Y/L1/R1/SELECT/START`. Two players
(`input_buttons(1)`). Analog sticks: reserved (`input_state()` zeros them).

## Audio

One stereo 48 kHz stream. The idiom: synthesize exactly **one display frame of
sound per game frame** (48000/60 = 800 frames) and push it:

```c
static int16_t abuf[2*800];
// fill abuf: interleaved L,R signed 16-bit
audio_stream_write(abuf, 800);   // blocks if the 42 ms hardware FIFO is full
```

Because the write blocks on the real sample clock, audio is also your safety
pacing if video ever runs ahead. For SFX, a square-wave "voice" struct
(pitch = half-period in samples, duration in frames) goes a long way — see
`pong`'s `beep()`/`audio_frame()`. Mix multiple voices by summing before write.

## Files (assets)

The user picks a file into the **Pak** slot from the Pocket menu; the game
pulls it with:

```c
pak_file_t f;
if (pak_open("pak", &f) == 0) {        // pulls the whole file into DRAM
    pak_read(&f, dst, n);              // then plain reads/seeks
    pak_seek(&f, off, 0);              // 0=SET 1=CUR 2=END
}
```

Rules: pak files must be **padded by ≥2 bytes** (APF quirk: the loader never
pulls a file's last 2 bytes). `pak_open` blocks while pulling (~MB/s); do it at
load screens. The data lands at a fixed DRAM area (3 MB max today) — `f.base`
points straight at it, so zero-copy access (`(uint8_t*)f.base`) is fine.

## Sound effects the easy way (voices)

Instead of hand-mixing, register one-shot mono clips on up to 4 voices and let
the HAL mix them:

```c
pcm_play(-1, boom_pcm, boom_len, 22050);   // -1 = any free voice, any rate
audio_pump();                              // once per frame, INSTEAD of
                                           // audio_stream_write()
```

## Saves

The console persists 4 KB across sessions (the host writes it to SD when you
exit the core cleanly). It's shared by all games — tag your record:

```c
struct { uint32_t magic, best; } sav;
if (save_read(0, &sav, sizeof sav) == sizeof sav && sav.magic == MY_MAGIC) ...
save_write(0, &sav, sizeof sav);           // on change, not per frame (~us/word)
```

## Exiting

`sys_exit()` reboots to the game picker (never returns). Convention:
SELECT+START. Note: saves persist to SD only when the user exits the *core*
via the Pocket menu — `sys_exit()` keeps you inside the console.

## Analog sticks

`input_state(0, &pad)` fills `pad.lx/ly/rx/ry` with signed 16-bit positions
(0-centered) when a dock/analog controller is present; d-pad-only controllers
read 0. Buttons are always in `pad.buttons`.

## Timing

`sys_ticks_us()` — free-running microsecond counter (wraps ~71 min; subtract,
don't compare). `sys_delay_us(n)` busy-waits. Most games need neither: the
frame loop is the clock.

## Memory (what's yours)

| Range | |
|---|---|
| `0x40400000` + 28 MB | your binary + globals; **heap after `_end`** (no malloc yet — use statics or a bump allocator from `_end`) |
| stack | 16 KB on-chip SRAM (fast; don't recurse deep) |
| `0x40100000` + 3 MB | pak data (via `pak_open`) |
| below that | framebuffers — only via `fb_backbuffer()` |

## Build & ship

```sh
cd sdk/yourgame                # Makefile: GAME=yourgame, GAME_SRCS=..., include ../game.mk
make                           # -> yourgame.bin (flat binary, auto-padded)
# copy to SD, pick in the Game slot. Re-pick to relaunch after changes.
```

`game.mk` links `hal.c` and LiteX's libc into your game; `printf()` works and
goes to the debug UART (dbg pins — needs the DevKey breakout to see).

## Debugging without a debugger

- **Visual codes**: paint a corner rectangle a distinct color per state — the
  screen is your best probe (this project was brought up that way).
- `printf` → UART at 115200 on the Pocket's dbg pins, if you have the breakout.
- A hang right after picking your game usually means a crash in early init:
  check that globals needing `.bss` aren't used before `main` and that your
  binary stays under the 28 MB region.
- The bootloader screen returning = your game never took over (bad binary).

## Worked examples

- `sdk/demo` — minimal: pattern drawing, box steering, beeps, pak background.
- `sdk/pong` — a complete game: states (title/play/over), edge-triggered
  input, english/spin physics in 8.3 fixed point, score text, three SFX.
