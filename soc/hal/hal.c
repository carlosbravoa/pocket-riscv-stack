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
