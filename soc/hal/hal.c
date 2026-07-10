// hal.c — RISC-V stack HAL implementation (Layer 2).
//
// Wraps the SoC's Layer-1 registers (csr.h / mem.h) into the app-facing API of
// hal.h. Apps never see anything below this file.
//
// Video: two framebuffers live in external DRAM; LiteX's VideoFramebuffer DMAs the
// front one to the scanout. fb_present() flips by retargeting the DMA at the page
// we just drew, synchronized to the DMA's frame wrap (see below) so no displayed
// frame ever mixes two rendered frames.
//
// SPDX-License-Identifier: BSD-2-Clause

#include "hal.h"

#include <string.h>
#include <system.h>
#include <liblitedram/sdram.h>
#include <generated/csr.h>
#include <generated/mem.h>
#include <generated/soc.h>

// Frame geometry comes from the SoC build (single source: pocket_soc.py emits
// these into generated/soc.h). Never hardcode 320/240 here.
#define FB_W VIDEO_FRAMEBUFFER_HRES
#define FB_H VIDEO_FRAMEBUFFER_VRES
#define FB_PAGE_BYTES (FB_W * FB_H)                 // rgb332: 1 byte/pixel

// Two framebuffer pages, low in DRAM (the CPU boots from ROM / stacks in SRAM, so
// low main_ram is free; apps allocate heap higher up).
static const uint32_t page_addr[2] = {
	MAIN_RAM_BASE + 0x00000000,
	MAIN_RAM_BASE + 0x00020000,                     // 128 KB apart
};
static int draw_page;                               // page the CPU draws into (back)

void sys_init(void)
{
	// Warm start (a game launched by the bootloader): DRAM and the scanout are
	// already live — re-training the SDRAM under an active DMA would glitch or
	// corrupt. Adopt the running state: draw into whichever page is hidden.
	if (video_framebuffer_dma_enable_read()) {
		draw_page = (video_framebuffer_dma_base_read()
		             == page_addr[0]) ? 1 : 0;
		return;
	}

	// DRAM first — everything else (framebuffers, future asset loading) lives there.
	// On failure: report on the diag register and halt with the video DMA off, so the
	// failure is a distinct signature (frozen screen + diag code), not random garbage.
	if (!sdram_init()) {
		diag_out_write(0xDEADD3A2);                 // "DEAD DRAM"
		for (;;)
			;
	}

	// Both pages start as power-on DRAM noise; clear them (and write the zeros back
	// to DRAM) so boot shows black, not garbage, until the first fb_present().
	memset((void *)page_addr[0], 0, FB_PAGE_BYTES);
	memset((void *)page_addr[1], 0, FB_PAGE_BYTES);
	flush_cpu_dcache_range((void *)page_addr[0], FB_PAGE_BYTES);
	flush_cpu_dcache_range((void *)page_addr[1], FB_PAGE_BYTES);

	// Bring up the scanout showing page 1 while we draw page 0. Order matters on a
	// warm restart: stop the DMA before moving its base, then enable.
	video_framebuffer_dma_enable_write(0);
	video_framebuffer_vtg_enable_write(0);
	video_framebuffer_dma_base_write(page_addr[1]);
	video_framebuffer_vtg_enable_write(1);
	video_framebuffer_dma_enable_write(1);
	draw_page = 0;
}

int  fb_width(void)  { return FB_W; }
int  fb_height(void) { return FB_H; }

void palette_set(const uint8_t rgb[256][3])
{
	// One CSR write per entry: {index, R, G, B}. Writes land mid-scanout, so a
	// full reload can straddle a frame boundary — call right after fb_present()
	// (start of the hidden-frame window) for glitch-free fades.
	for (int i = 0; i < 256; i++)
		main_palette_write(((uint32_t)i << 24)
		                   | ((uint32_t)rgb[i][0] << 16)
		                   | ((uint32_t)rgb[i][1] << 8)
		                   |  (uint32_t)rgb[i][2]);
}

const hal_caps_t *sys_caps(void)
{
	static hal_caps_t caps;
	caps.fb_w           = FB_W;
	caps.fb_h           = FB_H;
	caps.fb_bpp         = 8;
	caps.main_ram_bytes = MAIN_RAM_SIZE;
	caps.cpu_hz         = CONFIG_CLOCK_FREQUENCY;
	caps.features       = HAL_FEAT_PALETTE | HAL_FEAT_PCM
	                    | HAL_FEAT_PAD2    | HAL_FEAT_PAK;
	return &caps;
}

uint8_t *fb_backbuffer(void)
{
	return (uint8_t *)page_addr[draw_page];
}

void fb_present(void)
{
	// The framebuffer is cached and the DMA reads DRAM directly: write our pixels
	// back before the scanout can fetch this page. (flush_cpu_dcache() is a NO-OP
	// on VexiiRiscv — the Zicbom range flush is the real one.)
	flush_cpu_dcache_range((void *)page_addr[draw_page], FB_PAGE_BYTES);

	// Tear-free flip: the DMA's base register takes effect IMMEDIATELY (it is not
	// latched at frame boundaries), so retarget it exactly when the DMA wraps to a
	// new frame — offset resets to ~0. At that instant every remaining pixel of the
	// on-screen frame is already buffered in the (small) scanout FIFO, so:
	//   - the next fetched frame comes entirely from the new page (no mixed frame),
	//   - the retiring page is no longer read from DRAM at all, so we can hand it
	//     out as the next back buffer immediately — no extra vsync wait, and the
	//     wrap-per-frame naturally paces the app at the display rate.
	// Bounded wait (>1 frame) so a disabled video path can't hang the app.
	uint32_t prev = video_framebuffer_dma_offset_read();
	for (int i = 0; i < 400000; i++) {
		uint32_t cur = video_framebuffer_dma_offset_read();
		if (cur < prev)
			break;                                  // wrapped: fetching frame start
		prev = cur;
	}
	video_framebuffer_dma_base_write(page_addr[draw_page]);
	draw_page ^= 1;
}

// ---------------------------------------------------------------------------
// Input — APF cont1/cont2 snapshots. input_poll() latches once; the per-frame
// call pattern gives apps a stable view for the whole frame.
// ---------------------------------------------------------------------------

static uint32_t pad_raw[2];

void input_poll(void)
{
	pad_raw[0] = main_cont1_read();
	pad_raw[1] = main_cont2_read();
}

uint32_t input_buttons(int player)
{
	return pad_raw[player & 1] & 0xffff;            // [15:0] = APF key bitmap
}

void input_state(int player, hal_pad_t *out)
{
	out->buttons = (uint16_t)input_buttons(player);
	out->lx = out->ly = out->rx = out->ry = 0;      // analog: dock support later
}

// ---------------------------------------------------------------------------
// Audio — 48 kHz signed 16-bit stereo stream into the SoC sample FIFO.
// ---------------------------------------------------------------------------

#define AUDIO_RATE       48000
#define AUDIO_FIFO_DEPTH 2048

int audio_stream_open(int rate)
{
	return (rate == AUDIO_RATE) ? 0 : -1;       // hardware rate is fixed
}

int audio_stream_write(const int16_t *pcm, int nframes)
{
	// pcm = interleaved stereo frames (L,R). Blocks on FIFO backpressure, so a
	// caller pushing more than the FIFO holds is paced to the real 48 kHz drain.
	for (int i = 0; i < nframes; i++) {
		while (main_audio_level_read() >= AUDIO_FIFO_DEPTH - 8)
			;
		main_audio_sample_write((uint16_t)pcm[2 * i]
		                        | ((uint32_t)(uint16_t)pcm[2 * i + 1] << 16));
	}
	return nframes;
}

// ---------------------------------------------------------------------------
// Pak — deferred APF data slot pulled into DRAM, exposed as file reads.
// The Pocket host owns the SD card; we ask it for byte ranges of the picked
// file (target_dataslot_read via the SoC's pak CSRs) and DMA them into
// main_ram at PAK_RAM_OFFSET.
// ---------------------------------------------------------------------------

#define PAK_BASE  (MAIN_RAM_BASE + PAK_RAM_OFFSET)
#define GAME_BASE (MAIN_RAM_BASE + GAME_RAM_OFFSET)
#define PAK_CHUNK 65536

static int pak_pull(uint32_t dst_off, uint32_t offset, uint32_t length)
{
	// dst_off: byte offset in main_ram where this chunk lands. The host writes
	// each chunk at bridgeaddr+0.., so the destination MUST advance per chunk.
	main_pak_dst_write(dst_off);
	main_pak_offset_write(offset);
	main_pak_length_write(length);
	main_pak_req_write(!main_pak_req_read());   // toggle = issue
	// The FSM raises busy within a few us; a full chunk takes ms (SD + bridge).
	uint32_t t0 = sys_ticks_us();
	while (!main_pak_busy_read() && (sys_ticks_us() - t0) < 10000)
		;
	while (main_pak_busy_read() && (sys_ticks_us() - t0) < 2000000)
		;
	if (main_pak_busy_read())
		return -1;                              // stuck (watchdog should prevent)
	return main_pak_err_read() ? -1 : 0;
}

// Pull a whole slot into main_ram at dst_off. id/dtaddr per data.json:
// slot "Pak" id 1 / index 0 -> dtaddr 1; slot "Game" id 2 / index 1 -> dtaddr 3.
static int pak_load_slot(uint16_t id, uint16_t dtaddr, uint32_t dst_off,
                         int wait_pick, pak_file_t *out)
{
	main_pak_id_write(id);
	main_pak_dtaddr_write(dtaddr);
	// The size readback crosses sys -> clk_74a (dtaddr) -> datatable BRAM ->
	// clk_74a -> sys (~200 ns round trip): reading immediately can return the
	// PREVIOUS slot's size (e.g. the game's own byte count, left by the
	// bootloader). Give the selector a generous settle before trusting it.
	sys_delay_us(100);
	// The host populates slot sizes in the data table; they can lag boot.
	// Size 0 after the wait = no file picked for this slot.
	uint32_t size = 0;
	for (int i = 0; i < (wait_pick ? 50 : 2) && (size = main_pak_size_read()) == 0; i++)
		sys_delay_us(20000);
	if (size == 0)
		return -1;

	// APF wedge bug: a dataslot read whose last byte touches EOF never
	// completes. Pull only size-2 bytes; ship paks padded by >=2 bytes.
	uint32_t usable = (size > 2) ? size - 2 : 0;
	for (uint32_t off = 0; off < usable; ) {
		uint32_t chunk = usable - off;
		if (chunk > PAK_CHUNK) chunk = PAK_CHUNK;
		if (pak_pull(dst_off + off, off, chunk) != 0)
			return -2;
		off += chunk;
	}
	// The DMA wrote DRAM behind the CPU's back: drop stale cache lines.
	flush_cpu_dcache_range((void *)(MAIN_RAM_BASE + dst_off), usable);

	out->base = MAIN_RAM_BASE + dst_off;
	out->size = usable;
	out->pos  = 0;
	return 0;
}

int pak_open(const char *name, pak_file_t *out)
{
	(void)name;                                 // assets slot ("Pak", id 1)
	return pak_load_slot(1, 1, PAK_RAM_OFFSET, 1, out);
}

int pak_load_game(pak_file_t *out)
{
	// Game slot (id 2): pulled to GAME_RAM_OFFSET where the binary executes.
	// No pick-wait: the bootloader polls in its own loop.
	return pak_load_slot(2, 3, GAME_RAM_OFFSET, 0, out);
}

void pak_run_game(const pak_file_t *g)
{
	// The binary was DMA'd into DRAM: data cache lines are already dropped by
	// the load; the INSTRUCTION cache must be invalidated before we execute it.
	flush_cpu_icache();
	((void (*)(void))g->base)();
	// A returning game falls back to the caller (bootloader loop).
}

int pak_read(pak_file_t *f, void *dst, int nbytes)
{
	uint32_t left = f->size - f->pos;
	uint32_t n = (nbytes < 0) ? 0 : (uint32_t)nbytes;
	if (n > left) n = left;
	memcpy(dst, (const void *)(f->base + f->pos), n);
	f->pos += n;
	return (int)n;
}

int pak_seek(pak_file_t *f, int offset, int whence)
{
	int64_t p = (whence == 1) ? (int64_t)f->pos + offset :
	            (whence == 2) ? (int64_t)f->size + offset : offset;
	if (p < 0 || p > (int64_t)f->size)
		return -1;
	f->pos = (uint32_t)p;
	return 0;
}

uint32_t sys_ticks_us(void)
{
#ifdef CSR_TIMER0_UPTIME_CYCLES_ADDR
	timer0_uptime_latch_write(1);
	uint64_t c = timer0_uptime_cycles_read();
	return (uint32_t)(c / (CONFIG_CLOCK_FREQUENCY / 1000000));
#else
#error "SoC has no timer0 uptime CSR (pocket_soc.py must call timer0.add_uptime())"
#endif
}

void sys_delay_us(uint32_t us)
{
	uint32_t t0 = sys_ticks_us();
	while ((sys_ticks_us() - t0) < us)
		;
}
