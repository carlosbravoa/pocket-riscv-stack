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

// Exit the game back to the bootloader's picker (does not return; the console
// reboots with autoload suppressed). backed by: main_exit CSR -> core_top
// skip-autoload flag + SoC reset pulse.                              [BUILT]
void      sys_exit(void);

// Capability table — the portability anchor. Sizes/clocks/feature bits are READ,
// never hardcoded, so the same app can run on a bigger SoC later.
// backed by: build-time constants from the SoC's generated headers.   [BUILT]
#define HAL_FEAT_PALETTE (1u << 0)
#define HAL_FEAT_PCM     (1u << 1)
#define HAL_FEAT_PAD2    (1u << 2)
#define HAL_FEAT_PAK     (1u << 3)
#define HAL_FEAT_FM      (1u << 4)   // OPL (planned fork)
#define HAL_FEAT_SAVE    (1u << 5)

typedef struct {
	uint16_t fb_w, fb_h;        // framebuffer geometry
	uint8_t  fb_bpp;            // 8 (indexed)
	uint32_t main_ram_bytes;    // external DRAM size
	uint32_t cpu_hz;            // sys_clk
	uint32_t features;          // HAL_FEAT_* bits
} hal_caps_t;
const hal_caps_t *sys_caps(void);                                    // [BUILT]

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

// Load the 256-entry palette (RGB888). The framebuffer byte becomes an index
// into it. At reset the palette IS the rgb332 mapping, so games that never call
// this render as before. Reload right after fb_present() for glitch-free fades.
// backed by: 256x24 palette BRAM in the scanout + main_palette CSR.   [BUILT]
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

// OPL3 register write (reg 0x000-0x0FF bank 0, 0x100-0x1FF bank 1). The
// design's thesis: the CPU never synthesizes FM — it just forwards the
// register writes its music engine already emits. FM output is hardware-mixed
// with the PCM stream. Remember: OPL3 mode needs reg 0x105 bit0, and each
// channel's 0xC0 needs the L/R output bits (0x30).
// backed by: opl3_fpga core + main_opl_cmd CSR.    [BUILT on the opl3 fork]
void      opl_write(uint16_t reg, uint8_t val);

// Fire-and-forget PCM sound effect on a voice (mono, any rate <= 48k; 4 voices;
// ch = -1 picks a free one, returns the channel or -1). Games using voices call
// audio_pump() once per frame INSTEAD of audio_stream_write() — it mixes the
// active voices into one display frame of stream. Software-mixed in the HAL.
int       pcm_play(int ch, const int16_t *pcm, int nsamples, int rate); // [BUILT]
void      audio_pump(void);                                            // [BUILT]

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

// Boot flow (used by the ROM bootloader; games never call these): pull the Game
// slot binary to its DRAM execution address, then invalidate the icache and jump.
// Games are flat binaries linked at the SDK's GAME base (see sdk/game.ld).
int       pak_load_game(pak_file_t *out);                             // [BUILT]
void      pak_run_game(const pak_file_t *g);                          // [BUILT]

// ============================================================================
// Save data — 4 KB persisted by the Pocket host (nonvolatile data slot: loaded
// from SD at core start, written back when the user exits the core cleanly).
// Shared by all games on this core: prefix your record with a magic. Byte-
// addressed; ~4 us/word behind the scenes, so read once at boot, write on
// change (not per frame).
// backed by: save BRAM in core_top + main_save_* CSR handshake.       [BUILT]
// ============================================================================

#define SAVE_SIZE 4096

// Ask the host to persist the save to SD immediately (target_dataslot_write).
// Called automatically by sys_exit(); call after writing a record you must not
// lose to a power-off. Costly (~ms) — never per frame.
int       save_flush(void);                                           // [BUILT]

int       save_read(uint32_t off, void *dst, uint32_t n);             // [BUILT]
int       save_write(uint32_t off, const void *src, uint32_t n);      // [BUILT]

#endif // RVSTACK_HAL_H
