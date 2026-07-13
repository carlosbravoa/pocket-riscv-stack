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
#include <irq.h>
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

void sys_diag(uint32_t v)
{
	// 32-bit debug word on the SoC's diag GPIO — visible to the sim testbench
	// and (rendered) by older bring-up cores. Games may use it freely.
	diag_out_write(v);
}

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
	// Feature bits come from the FLAVOR (core_top drives the hwfeat CSR:
	// base 0x2F, FM 0x3F) — the whole point of the family ABI. v0.15.x
	// detected FM at compile time (#ifdef CSR_MAIN_OPL_CMD_ADDR); the v0.16.0
	// family rework removed that (the CSR now exists everywhere) but forgot
	// this read — FM flavors reported no FM once binaries were rebuilt
	// (v0.17.1+), so fmdemo fell back to silence. Found by soc/sim.
	caps.features       = main_hwfeat_read();
	return &caps;
}

static void fb_flip_complete(void);

uint8_t *fb_backbuffer(void)
{
	// Completes a deferred flip (see fb_present_internal): the wrap-wait for
	// the PREVIOUS frame lands here, after it overlapped the caller's logic.
	fb_flip_complete();
	return (uint8_t *)page_addr[draw_page];
}

static void fb_present_internal(void);

void fb_present_dma(void)
{
	// For frames composed BY THE BLITTER: the data is already in DRAM, so the
	// page-wide dcache flush would be pure waste. Any CPU-drawn overlay must
	// have been range-flushed by the caller.
	fb_present_internal();
}

void fb_present(void)
{
	// The framebuffer is cached and the DMA reads DRAM directly: write our pixels
	// back before the scanout can fetch this page. (flush_cpu_dcache() is a NO-OP
	// on VexiiRiscv — the Zicbom range flush is the real one.)
	flush_cpu_dcache_range((void *)page_addr[draw_page], FB_PAGE_BYTES);
	fb_present_internal();
}

// Tear-free flip, DEFERRED: the DMA's base register takes effect IMMEDIATELY
// (not latched at frame boundaries), so it must be retargeted exactly when the
// DMA wraps to a new frame — offset resets to ~0. At that instant every
// remaining pixel of the on-screen frame is already buffered in the (small)
// scanout FIFO, so the next fetched frame comes entirely from the new page and
// the retiring page is free to become the next back buffer.
//
// The wrap-wait used to live INSIDE fb_present() — the CPU burned up to a full
// frame period spinning right after composing (15% of all CPU at RTL profile,
// v0.19.4, 20 fps game). Now fb_present() only marks the flip PENDING and
// returns; fb_backbuffer() completes it before handing out the page. The wait
// thus overlaps the game's logic/compose for the NEXT frame — by the time a
// slow game asks for the back buffer, the wrap has long since happened and the
// wait costs ~nothing. Fast games still pace at the display rate as before.
// Bounded wait (>1 frame) so a disabled video path can't hang the app.
static int      flip_pending;
static uint32_t flip_seen;              // last scanout offset observed pending

// Non-blocking: if the scanout has wrapped since fb_present(), retarget NOW.
// Called opportunistically (audio pump, PollEvent, Delay) so event-driven
// apps — menus that redraw only on input — get their frame on screen within
// one refresh instead of at their NEXT redraw (hardware v0.19.5: every menu
// press appeared to apply one press late). It also unquantizes pacing: a
// frame slower than one refresh finds its wrap already observed and flips
// instantly at the next fb_backbuffer(), so a 22 ms frame runs at ~45 fps
// instead of being punished down to the 30 fps vsync multiple.
void fb_flip_poll(void)
{
	if (!flip_pending)
		return;
	uint32_t cur = video_framebuffer_dma_offset_read();
	if (cur < flip_seen) {                          // wrapped since last look
		video_framebuffer_dma_base_write(page_addr[draw_page]);
		draw_page ^= 1;
		flip_pending = 0;
	} else {
		flip_seen = cur;
	}
}

static void fb_flip_complete(void)
{
	if (!flip_pending)
		return;
	fb_flip_poll();                                 // wrap already seen? free
	if (!flip_pending)
		return;
	uint32_t prev = flip_seen;
	for (int i = 0; i < 400000; i++) {
		uint32_t cur = video_framebuffer_dma_offset_read();
		if (cur < prev)
			break;                                  // wrapped: fetching frame start
		prev = cur;
	}
	video_framebuffer_dma_base_write(page_addr[draw_page]);
	draw_page ^= 1;
	flip_pending = 0;
}

static void fb_present_internal(void)
{
	// A caller that composed without fb_backbuffer() (its contract) still
	// gets the old strict behavior: finish the previous flip first.
	fb_flip_complete();
	flip_pending = 1;
	flip_seen = video_framebuffer_dma_offset_read();
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
	uint32_t joy = (player & 1) ? main_joy2_read() : main_joy1_read();
	out->buttons = (uint16_t)input_buttons(player);
	// APF joy: unsigned bytes, 128-centered: [7:0] lx, [15:8] ly, [23:16] rx,
	// [31:24] ry. Scale to +/-32512 (x256). Zero when no analog pad present
	// (the APF reports 128 center for absent sticks -> 0 here too).
	out->lx = (int16_t)((int)((joy >>  0) & 0xFF) - 128) * 256;
	out->ly = (int16_t)((int)((joy >>  8) & 0xFF) - 128) * 256;
	out->rx = (int16_t)((int)((joy >> 16) & 0xFF) - 128) * 256;
	out->ry = (int16_t)((int)((joy >> 24) & 0xFF) - 128) * 256;
}

// ---------------------------------------------------------------------------
// Blitter — see hal.h. Pointer -> main_ram byte-offset conversion here.
// ---------------------------------------------------------------------------

static int blit_internal(void *dst, const void *src, uint32_t w_bytes,
                         uint32_t h_rows, uint32_t src_stride,
                         uint32_t dst_stride, uint32_t flags);

int blit(void *dst, const void *src, uint32_t w_bytes, uint32_t h_rows,
         uint32_t src_stride, uint32_t dst_stride)
{
	return blit_internal(dst, src, w_bytes, h_rows, src_stride, dst_stride, 0);
}

int blit_ck(void *dst, const void *src, uint32_t w_bytes, uint32_t h_rows,
            uint32_t src_stride, uint32_t dst_stride)
{
	if (!(sys_caps()->features & HAL_FEAT_BLITKEY))
		return -1;
	return blit_internal(dst, src, w_bytes, h_rows, src_stride, dst_stride, 1);
}

static int blit_internal(void *dst, const void *src, uint32_t w_bytes,
                         uint32_t h_rows, uint32_t src_stride,
                         uint32_t dst_stride, uint32_t flags)
{
	if (!(sys_caps()->features & HAL_FEAT_BLIT))
		return -1;
	main_blit_src_write((uint32_t)((uintptr_t)src - MAIN_RAM_BASE));
	main_blit_dst_write((uint32_t)((uintptr_t)dst - MAIN_RAM_BASE));
	main_blit_sstride_write(src_stride);
	main_blit_dstride_write(dst_stride);
	main_blit_w_write(w_bytes);
	main_blit_h_write(h_rows);
	main_blit_flags_write(flags);
	main_blit_kick_write(1);
	return 0;
}

void blit_wait(void)
{
	while (main_blit_busy_read())
		;
}

// ---------------------------------------------------------------------------
// Saves — one file per game, created on demand. The 4 KB window BRAM in
// core_top is only a transfer buffer: bytes 0..0xDFF carry data chunks in
// both directions, bytes 0xE00..0xF07 hold the open_dataslot_file_t the host
// reads when we issue target_dataslot_openfile. Save data itself lives in a
// DRAM staging area (SAVE_RAM_OFFSET, 1 MB budget). Window access is a
// word-at-a-time toggle handshake, ~4 us/word.
// ---------------------------------------------------------------------------

static int pak_pull(uint32_t dst_off, uint32_t offset, uint32_t length);

static void save_hs_settle(void)
{
	// adr/wdat and the wr/rd toggles cross clock domains independently; give
	// the address time to land before the toggle can be observed.
	sys_delay_us(2);
}

static int save_hs_wait(uint32_t ack0)
{
	uint32_t t0 = sys_ticks_us();
	while (main_save_ack_read() == ack0)
		if ((sys_ticks_us() - t0) > 1000)
			return -1;
	return 0;
}

static uint16_t save_word_read(uint32_t wadr)
{
	main_save_adr_write(wadr);
	save_hs_settle();
	uint32_t a = main_save_ack_read();
	main_save_rd_write(!main_save_rd_read());
	save_hs_wait(a);
	save_hs_settle();                               // rdat crosses after ack
	return (uint16_t)main_save_rdat_read();
}

static void save_word_write(uint32_t wadr, uint16_t v)
{
	main_save_adr_write(wadr);
	main_save_wdat_write(v);
	save_hs_settle();
	uint32_t a = main_save_ack_read();
	main_save_wr_write(!main_save_wr_read());
	save_hs_wait(a);
}

#define SAVE_SLOT_ID    3
#define SAVE_WIN_CHUNK  0x0E00u                 // window data area: 3584 bytes
#define SAVE_WIN_STRUCT 0x0E00u                 // open_dataslot_file_t at +0xE00
#define SAVE_BUDGET     0x00100000u             // staging area (SAVE_RAM_OFFSET)
#define SAVE_PAD        4                       // file = cap+4: chunk reads/writes
                                                // never touch EOF (APF wedge)

static char     save_bound[64];                 // path currently bound to the slot
static uint32_t save_alloc;                     // staging bytes handed out
static uint32_t save_hw_err;                    // last pak FSM err (diagnostics)
static int      save_cmd_wait(void);

uint32_t save_last_hw_err(void) { return save_hw_err; }

static int save_cmd_wait(void)
{
	// Command completion: busy rises within us, falls when the host is done
	// (SD create/resize can take a while). pak_err: 0 ok, 1 created (openfile
	// success!), 2..5 host result codes, 7 FSM watchdog.
	uint32_t t0 = sys_ticks_us();
	while (!main_pak_busy_read() && (sys_ticks_us() - t0) < 10000)
		;
	while (main_pak_busy_read() && (sys_ticks_us() - t0) < 2000000)
		;
	if (main_pak_busy_read())
		return -1;
	save_hw_err = main_pak_err_read();
	return (int)save_hw_err;
}

// Window layout (4 KB, persisted VERBATIM as <game>.sav by the host):
//   0x000 u32 magic "RVSV" | 0x004 u32 nfiles |
//   0x008 entries[8] { char name[24]; u32 off; u32 size }  (32 B each)
//   0x108.. file data. The host loads the .sav here before the game runs
//   (bit5 fills 0xFF when there is no file yet) and writes it back to SD at
//   core quit / power-off / sleep — the SNES mechanism; no target commands.
#define SAVE_TOC_MAGIC  0x56535652u         // "RVSV"
#define SAVE_TOC_MAX    8
#define SAVE_TOC_ENTRY  32
#define SAVE_DATA_BASE  (8 + SAVE_TOC_MAX * SAVE_TOC_ENTRY)
#define SAVE_WIN_TOTAL  32768               // 32 KB window (v0.18.0)

static uint32_t win_rd32(uint32_t off)
{
	return (uint32_t)save_word_read(off >> 1)
	     | ((uint32_t)save_word_read((off + 2) >> 1) << 16);
}

static void win_wr32(uint32_t off, uint32_t v)
{
	save_word_write(off >> 1, (uint16_t)v);
	save_word_write((off + 2) >> 1, (uint16_t)(v >> 16));
}

static void win_read(uint32_t off, void *dst, uint32_t n)
{
	uint8_t *d = dst;
	for (uint32_t i = 0; i < n; i += 2) {
		uint16_t w = save_word_read((off + i) >> 1);
		d[i] = (uint8_t)w;
		if (i + 1 < n)
			d[i + 1] = (uint8_t)(w >> 8);
	}
}

static void win_write(uint32_t off, const void *src, uint32_t n)
{
	const uint8_t *s = src;
	for (uint32_t i = 0; i < n; i += 2)
		save_word_write((off + i) >> 1,
		    (uint16_t)(s[i] | ((i + 1 < n ? s[i + 1] : 0) << 8)));
}

// Publish the save's true byte count into the APF datatable (word 5): the
// host reads it during the flush and sizes the .sav file accordingly.
static void save_publish_size(uint32_t bytes)
{
	main_save_dtsize_write(bytes);
	sys_delay_us(10);
	main_save_szset_write(!main_save_szset_read());
	sys_delay_us(10);
}

// The host loads nonvolatile slots at CORE start — but our Game slot is
// deferload, so at that moment no game is picked and the derived <game>.sav
// name resolves to nothing: the window stays empty even when a save file
// exists (hardware v0.17.7: file created on quit, never loaded on relaunch).
// Recovery: once per boot, if the window has no TOC, pull the slot's file
// content ourselves over the proven dataslot-read path and re-image the
// window from it. Read length must stop short of EOF (the APF wedge), which
// is why save_publish_size() pads the file by 4 bytes at commit.
typedef struct { char name[24]; uint32_t off, size; } save_ent_t;

static uint32_t save_restore_state = 9;     // 9 = not attempted yet
uint32_t save_restore_code(void) { return save_restore_state; }

static void save_window_restore(void)
{
	static int tried;
	if (tried)
		return;
	tried = 1;
	if (win_rd32(0) == SAVE_TOC_MAGIC) {
		save_restore_state = 1;             // window already carries a TOC
		return;
	}
	// Two-stage read sized from OUR OWN metadata — the host's slot table is
	// compacted to loaded slots (position != declaration order), so a fixed
	// table word can silently read 0 (hardware v0.17.8/9: saves created but
	// never restored). Stage 1: the fixed-size TOC (header + entry array,
	// SAVE_DATA_BASE bytes — every committed file is at least that + 4 pad,
	// so this never touches EOF). Stage 2: the data region, sized from the
	// entries. If the slot has no file bound, stage 1 errors and we run
	// fresh — that's the first-boot path.
	uint32_t tmp = SAVE_RAM_OFFSET + SAVE_BUDGET - SAVE_WIN_TOTAL;
	main_pak_id_write(SAVE_SLOT_ID);
	main_pak_dtaddr_write(5);
	sys_delay_us(100);
	if (pak_pull(tmp, 0, SAVE_DATA_BASE) != 0) {
		save_restore_state = 2;             // no file / read refused
		return;
	}
	flush_cpu_dcache_range((void *)(MAIN_RAM_BASE + tmp), SAVE_DATA_BASE);
	const uint32_t *img = (const uint32_t *)(MAIN_RAM_BASE + tmp);
	if (img[0] != SAVE_TOC_MAGIC || img[1] > SAVE_TOC_MAX) {
		save_restore_state = 3;             // not our format: run fresh
		return;
	}
	const save_ent_t *e = (const save_ent_t *)(img + 2);
	uint32_t used = SAVE_DATA_BASE;
	for (uint32_t i = 0; i < img[1]; i++)
		if (e[i].off + e[i].size > used)
			used = e[i].off + e[i].size;
	if (used > SAVE_WIN_TOTAL) {
		save_restore_state = 3;
		return;
	}
	if (used > SAVE_DATA_BASE &&
	    pak_pull(tmp + SAVE_DATA_BASE, SAVE_DATA_BASE,
	             used - SAVE_DATA_BASE) != 0) {
		save_restore_state = 4;             // data stage failed: run fresh
		return;
	}
	flush_cpu_dcache_range((void *)(MAIN_RAM_BASE + tmp), used);
	win_write(0, img, used);
	save_restore_state = 0;                 // restored
}

static int toc_load(save_ent_t *ents, uint32_t *nf)
{
	if (win_rd32(0) != SAVE_TOC_MAGIC) {
		*nf = 0;
		return 0;                           // fresh window (0xFF or zeros)
	}
	uint32_t n = win_rd32(4);
	if (n > SAVE_TOC_MAX)
		return -1;                          // corrupt: treat as fresh
	win_read(8, ents, n * SAVE_TOC_ENTRY);
	*nf = n;
	return 0;
}

static void toc_store(const save_ent_t *ents, uint32_t nf)
{
	win_wr32(0, SAVE_TOC_MAGIC);
	win_wr32(4, nf);
	win_write(8, ents, nf * SAVE_TOC_ENTRY);
}

int save_open(const char *name, uint32_t size, save_file_t *f)
{
	if (!name || !f || !size)
		return -1;
	size = (size + 3) & ~3u;

	save_window_restore();                  // see comment above

	save_ent_t ents[SAVE_TOC_MAX];
	uint32_t nf = 0;
	if (toc_load(ents, &nf) != 0)
		nf = 0;

	// name -> entry (names are the identity; another game's leftover TOC
	// simply won't match and this game starts fresh)
	int idx = -1;
	uint32_t data_top = SAVE_DATA_BASE;
	for (uint32_t i = 0; i < nf; i++) {
		if (!strncmp(ents[i].name, name, sizeof(ents[i].name) - 1))
			idx = (int)i;
		if (ents[i].off + ents[i].size > data_top)
			data_top = ents[i].off + ents[i].size;
	}

	int created = 0;
	if (idx < 0) {
		if (nf >= SAVE_TOC_MAX || data_top + size > SAVE_WIN_TOTAL - 4)
			return -1;                      // window full (4 KB for now)
		idx = (int)nf++;
		memset(&ents[idx], 0, sizeof(ents[idx]));
		for (int i = 0; name[i] && i < (int)sizeof(ents[idx].name) - 1; i++)
			ents[idx].name[i] = name[i];
		ents[idx].off  = data_top;
		ents[idx].size = size;
		toc_store(ents, nf);
		created = 1;
	} else if (ents[idx].size != size) {
		return -1;                          // capacity is fixed at creation
	}

	// hand out a DRAM shadow; f->_path doubles as the entry name
	if (save_alloc + size > SAVE_BUDGET)
		return -1;
	f->base = MAIN_RAM_BASE + SAVE_RAM_OFFSET + save_alloc;
	f->size = size;
	save_alloc += size;
	for (int i = 0; i < (int)sizeof(f->_path); i++)
		f->_path[i] = (i < (int)sizeof(ents[idx].name)) ? ents[idx].name[i] : 0;

	if (created)
		memset((void *)f->base, 0, size);
	else
		win_read(ents[idx].off, (void *)f->base, size);
	return created;
}

int save_commit(save_file_t *f)
{
	if (!f || !f->base || !f->size)
		return -1;
	save_ent_t ents[SAVE_TOC_MAX];
	uint32_t nf = 0;
	if (toc_load(ents, &nf) != 0)
		return -2;
	int idx = -1;
	uint32_t used = SAVE_DATA_BASE;
	for (uint32_t i = 0; i < nf; i++) {
		if (!strncmp(ents[i].name, f->_path, sizeof(ents[i].name) - 1))
			idx = (int)i;
		if (ents[i].off + ents[i].size > used)
			used = ents[i].off + ents[i].size;
	}
	if (idx < 0)
		return -2;                          // window was re-imaged since open
	win_write(ents[idx].off, (const void *)f->base, f->size);
	// +4 pad: dataslot reads that touch EOF never complete (APF wedge), and
	// save_window_restore() reads this very file back at next boot.
	save_publish_size(used + 4);

	// The host GUARANTEES persistence at quit/power-off/sleep. Best effort
	// beyond that: a dataslot_write flush right now — works only once the
	// host has bound the file (after its first flush); errors are fine.
	main_pak_id_write(SAVE_SLOT_ID);
	main_pak_offset_write(0);
	main_pak_length_write(used);
	sys_delay_us(100);
	main_pak_wreq_write(!main_pak_wreq_read());
	save_cmd_wait();                        // result intentionally ignored
	return 0;
}

// ---------------------------------------------------------------------------
// Exit — hand control back to the bootloader's picker. core_top latches a
// skip-autoload flag (outside the SoC reset domain) and pulses the SoC reset.
// ---------------------------------------------------------------------------

void sys_exit(void)
{
	// Silence FM first: the OPL3 holds its last key-on state across the exit
	// reset (the reset pulses the SoC/CPU, not the OPL3's 12.288 domain), so
	// without this the last notes ring forever after quit (field v0.20.7).
	// key-off every channel, both banks; opl_write is a no-op without FM.
	if (sys_caps()->features & HAL_FEAT_FM) {
		for (int ch = 0; ch < 9; ch++) {
			opl_write(0xB0 + ch, 0x00);           // bank 0 key-off
			opl_write(0x1B0 + ch, 0x00);          // bank 1 key-off (OPL3)
		}
		opl_write(0xBD, 0x00);                     // rhythm off
	}
	// Pure hardware from here: the toggle latches skip-autoload and pulses
	// the SoC reset (~14 ms, the same proven path a Game re-pick uses).
	// Saves are the game's own explicit act (save_commit) — nothing to do.
	main_exit_write(!main_exit_read());
	for (;;)
		;
}

// ---------------------------------------------------------------------------
// FM synthesis — the CPU forwards register writes; synthesis is hardware on FM
// flavors. Two-port protocol: A0=0 address write (A1 = bank), A0=1 data. Part
// of the family ABI: on flavors without FM the bus dangles (harmless no-op) —
// gate music-path choice on sys_caps()->features & HAL_FEAT_FM. The 2us pacing
// guarantees each toggle crosses the clock-domain sync.
// ---------------------------------------------------------------------------

void opl_write(uint16_t reg, uint8_t val)
{
	// Fast-retrigger guard: the OPL3 envelope generators sample kon once per
	// ~20 us channel slot, so a keyoff->keyon rewrite of the SAME key-on
	// register (0xB0-0xB8, 0xBD rhythm) landing inside one slot is never
	// observed — the note simply doesn't retrigger (5/8 drums dropped at RTL,
	// fmtest retrigger experiment; field: missing percussion/fast leads).
	// DOS-era ISA writes were >=26 us apart and never hit this. Pace ONLY
	// same-register key-on rewrites; every other write keeps the fast path.
	static uint16_t opl_last_reg = 0xFFFF;
	static uint32_t opl_last_us;
	if (reg == opl_last_reg && ((reg & 0xF0) == 0xB0 || (reg & 0xFF) == 0xBD))
		while ((uint32_t)(sys_ticks_us() - opl_last_us) < 26)
			;
	uint32_t abus = (reg & 0x100) ? 2u : 0u;        // A1 selects the bank
	main_opl_cmd_write((abus << 8) | (reg & 0xFF)); // address port
	sys_delay_us(2);
	main_opl_cmd_write((1u << 8) | val);            // data port
	sys_delay_us(2);
	opl_last_reg = reg;
	opl_last_us  = sys_ticks_us();
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

int audio_stream_free(void)
{
	// Frames the 48 kHz FIFO can take RIGHT NOW without blocking. Pumps that
	// respect this keep UI loops honest: a blocking push inside SDL_Delay(1)
	// turned every menu tick into ~5 ms (Tyrian jukebox, hardware v0.17.9).
	uint32_t lvl = main_audio_level_read();
	return (lvl >= AUDIO_FIFO_DEPTH - 8) ? 0
	     : (int)(AUDIO_FIFO_DEPTH - 8 - lvl);
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

// --- pcm_play voices: a small software mixer over the stream ---------------
// Games that use voices call audio_pump() once per frame instead of
// audio_stream_write(). Voices are mono, resampled by a 16.16 phase step.

#define PCM_VOICES 4

static struct {
	const int16_t *pcm;                 // NULL = free
	uint32_t pos, step, len;            // pos/step in 16.16 samples
} voice[PCM_VOICES];

int pcm_play(int ch, const int16_t *pcm, int nsamples, int rate)
{
	if (ch < 0 || ch >= PCM_VOICES) {
		for (ch = 0; ch < PCM_VOICES && voice[ch].pcm; ch++)
			;
		if (ch == PCM_VOICES)
			return -1;                  // all voices busy
	}
	voice[ch].pcm  = pcm;
	voice[ch].pos  = 0;
	voice[ch].len  = (uint32_t)nsamples;
	voice[ch].step = (uint32_t)(((uint64_t)rate << 16) / AUDIO_RATE);
	return ch;
}

void audio_pump(void)
{
	static int16_t mix[2 * (AUDIO_RATE / 60)];
	const int n = AUDIO_RATE / 60;
	for (int i = 0; i < n; i++) {
		int32_t acc = 0;
		for (int v = 0; v < PCM_VOICES; v++) {
			if (!voice[v].pcm)
				continue;
			acc += voice[v].pcm[voice[v].pos >> 16];
			voice[v].pos += voice[v].step;
			if ((voice[v].pos >> 16) >= voice[v].len)
				voice[v].pcm = 0;       // one-shot done
		}
		if (acc >  32767) acc =  32767;
		if (acc < -32768) acc = -32768;
		mix[2 * i] = mix[2 * i + 1] = (int16_t)acc;
	}
	audio_stream_write(mix, n);
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
	// datatable index = array POSITION in data.json (v0.17.7: Game moved to
	// position 0 so save names derive from slot id 0) -> Pak is word 3.
	return pak_load_slot(1, 3, PAK_RAM_OFFSET, 1, out);
}

int pak_open_at(uint32_t dst_off, pak_file_t *out)
{
	// Big paks (Tyrian: 11.4 MB) don't fit the default 3 MB window below the
	// game image — the caller picks a destination offset in main_ram (e.g.
	// above the game region). Same slot, same pull, different landing zone.
	return pak_load_slot(1, 3, dst_off, 1, out);
}

int pak_load_game(pak_file_t *out)
{
	// Game slot (id 0, array position 0 -> datatable word 1): pulled to
	// GAME_RAM_OFFSET where the binary executes. Id 0 is load-bearing: the
	// save slot's bit2 derives <game>.sav from SLOT 0's picked file.
	// No pick-wait: the bootloader polls in its own loop.
	return pak_load_slot(0, 1, GAME_RAM_OFFSET, 0, out);
}

void pak_run_game(const pak_file_t *g)
{
	// Hand off cleanly: no interrupt may fire once the game owns SRAM (our
	// ISR state lives there). The game's crt0 also disables MIE first thing —
	// this closes the gap between the jump and that instruction.
	irq_setie(0);
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
