// hal.h — the RISC-V stack HAL (Layer 2: the opinionated middle layer)
//
// This is the DESIGN TARGET, not a finished API. Applications (Tyrian, Doom, ...)
// talk to THIS and nothing below it — never csr.h, never a raw MMIO address. That
// rule is what keeps the SoC (Layer 0) and the games (Layer 3) independent:
//
//   Layer 3  app        Tyrian logic / menus / its music engine        (game-specific)
//   Layer 2  HAL        THIS FILE — services a *class* of retro games   (opinionated)
//   Layer 1  BSP        crt0, picolibc, csr.h accessors, sdram_init     (this SoC, in C)
//   Layer 0  SoC        framebuffer, DRAM, video, OPL2, mixer, input    (registers only)
//
// The API is deliberately shaped for the CLASS "DOS/retro/SDL-style games", not for
// any one title. The test of a good line: Tyrian, Doom, Quake, Duke3D all sit on this
// unchanged; each game's quirks (input map, .pak layout, which audio calls it makes)
// live ABOVE this line, in the app's glue.
//
// "backed by:" ties each call to the Layer-0 hardware that implements it — the contract
// with the SoC. Status: [BUILT] works today, [PARTIAL] hardware exists but HAL is thin,
// [PLANNED] needs SoC + HAL work.
//
// SPDX-License-Identifier: BSD-2-Clause
#ifndef RVSTACK_HAL_H
#define RVSTACK_HAL_H

#include <stdint.h>
#include <stddef.h>

// ============================================================================
// System / lifecycle / capabilities
// ============================================================================

// One-time bring-up: SDRAM init, timer, input, audio. Call first.
// backed by: sdram_init() (LiteDRAM), CSR timer/uart.                 [PARTIAL]
void      sys_init(void);

// Free-running microsecond counter (wraps). The one time source games need.
// backed by: LiteX timer0 uptime CSR.                                [BUILT]
uint32_t  sys_ticks_us(void);
void      sys_delay_us(uint32_t us);                                 // [BUILT]

// Capability table — the portability anchor. Sizes/clocks/feature bits are READ,
// never hardcoded, so the same app .elf can run on a bigger SoC (or MiSTer) later.
// backed by: a small read-only CSR block filled from build-time params. [PLANNED]
typedef struct {
	uint16_t fb_w, fb_h;        // framebuffer geometry
	uint8_t  fb_bpp;            // 8 (indexed)
	uint32_t main_ram_bytes;    // external DRAM size
	uint32_t cpu_hz;            // sys_clk
	uint32_t features;          // HAL_FEAT_* bits (OPL2, PCM, PALETTE, PAD2, ...)
} hal_caps_t;
const hal_caps_t *sys_caps(void);                                    // [PLANNED]

// ============================================================================
// Video — indexed 8bpp framebuffer, double-buffered (the opinionated choice)
// ============================================================================
// The HAL hides double-buffering and vsync. Apps draw into the back buffer as a
// plain uint8_t[H][W] of palette indices, then present. No app ever touches the
// scanout, the page-flip register, or the palette RAM directly.

int       fb_width(void);                                            // [BUILT]
int       fb_height(void);                                           // [BUILT]

// Pointer to the HIDDEN draw buffer (palette indices). Valid until fb_present().
// backed by: framebuffer region (BRAM @0x8000_0000 today; moves to DRAM w/ scanout
//            DMA for true double-buffering).                          [PARTIAL]
uint8_t  *fb_backbuffer(void);

// Flip back<->front and wait for vsync. THE first HAL primitive that fixes the
// Stage-3 flicker. backed by: SoC page-flip register + vsync-status bit fed from
// core_top's frame counter.                                          [PLANNED]
void      fb_present(void);

// Load the 256-entry palette (RGB888). backed by: 256x24-bit palette RAM in the
// video scanout (replaces today's fixed RGB332 mapping).             [PLANNED]
void      palette_set(const uint8_t rgb[256][3]);

// ============================================================================
// Input — pads (bit-decoded for the app; class-general, not per-game mapping)
// ============================================================================
// backed by: main_cont1/cont2 CSRs (MultiReg-synced APF cont*_key).   [BUILT]
// APF key bitmap ([15:0]): 0 up, 1 down, 2 left, 3 right, 4 A, 5 B, 6 X, 7 Y,
// 8 L1, 9 R1, 10 L2, 11 R2, 12 L3, 13 R3, 14 select, 15 start.

#define HAL_BTN_UP     (1u << 0)
#define HAL_BTN_DOWN   (1u << 1)
#define HAL_BTN_LEFT   (1u << 2)
#define HAL_BTN_RIGHT  (1u << 3)
#define HAL_BTN_A      (1u << 4)
#define HAL_BTN_B      (1u << 5)
#define HAL_BTN_X      (1u << 6)
#define HAL_BTN_Y      (1u << 7)
#define HAL_BTN_L1     (1u << 8)
#define HAL_BTN_R1     (1u << 9)
#define HAL_BTN_SELECT (1u << 14)
#define HAL_BTN_START  (1u << 15)

typedef struct {
	uint16_t buttons;           // dpad/face/start/select bitmap (APF cont*_key layout)
	int16_t  lx, ly, rx, ry;    // analog sticks (dock), +/-32767
} hal_pad_t;

void      input_poll(void);                       // sample once per frame  [BUILT]
uint32_t  input_buttons(int player);              // 0=P1, 1=P2             [BUILT]
void      input_state(int player, hal_pad_t *out);                   // [BUILT]

// ============================================================================
// Audio — FM (OPL2) + PCM SFX + music stream (covers the class's audio needs)
// ============================================================================
// Opinionated but general: Tyrian leans on opl_write; a Quake port ignores it and
// uses the stream. The HAL offers the surface; each app uses the part it needs.

// AdLib/OPL2 register write. The design's thesis: the CPU never synthesizes FM —
// it just forwards the register writes its music engine already emits.
// backed by: hardware OPL2 block MMIO port.                          [PLANNED]
void      opl_write(uint8_t reg, uint8_t val);

// Fire-and-forget PCM sound effect on a voice. backed by: HW PCM/mixer voices. [PLANNED]
// (apps can software-mix into the stream below meanwhile)
int       pcm_play(int ch, const int16_t *pcm, int nsamples, int rate);

// Gapless PCM stream: 48 kHz, signed 16-bit, INTERLEAVED STEREO frames (L,R).
// write() blocks on the hardware FIFO (2048 frames ~= 42 ms), which paces the
// caller to the real sample clock. backed by: main_audio_sample/main_audio_level
// CSRs -> SoC FIFO -> 12.288MHz/256 drain -> sound_i2s -> APF DAC.      [BUILT]
int       audio_stream_open(int rate);                     // rate must be 48000
int       audio_stream_write(const int16_t *pcm, int nframes);

// ============================================================================
// Files — game assets via APF data slots (present as normal file reads)
// ============================================================================
// The Pocket host reads the SD; the "Pak" slot is deferload, so pak_open() pulls
// the picked file into DRAM chunk-by-chunk (target_dataslot_read) after boot.
// Apps see "open the file and read it", not the bridge FSM.
// backed by: main_pak_* CSRs -> core_top target_dataslot_read FSM +
//            data_loader -> DRAM DMA at PAK_RAM_OFFSET.               [BUILT]
// NOTE: usable size = file size - 2 (APF EOF-read wedge bug) — pad pak files.
// name is ignored for now (one slot); whence: 0=SET, 1=CUR, 2=END.

typedef struct { uint32_t base; uint32_t size; uint32_t pos; } pak_file_t;

int       pak_open(const char *name, pak_file_t *out);   // <0: none/failed [BUILT]
int       pak_read(pak_file_t *f, void *dst, int nbytes);             // [BUILT]
int       pak_seek(pak_file_t *f, int offset, int whence);            // [BUILT]

#endif // RVSTACK_HAL_H
