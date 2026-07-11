/*
 * of_files.c — OpenTyrian2000 riscv-stack port: file layer implementation.
 *
 * Why not pakfs_mount(): the HAL's pak_open() DMAs the picked Pak file to a
 * fixed 3 MB window at main_ram+0x100000 — right below the game image at
 * +0x400000. tyrian.pak is ~11 MB; pulling it there would overwrite the
 * running game. So this file drives the same pak-loader CSRs the HAL uses
 * (generated/csr.h) but points the DMA at otherwise-unclaimed DRAM above the
 * 28 MB game region and the save-staging megabyte:
 *
 *   0x40400000 +28MB = 0x42000000  game image + heap (gamelib HEAP_LIMIT)
 *   0x42000000 + 1MB = 0x42100000  HAL save staging (SAVE_RAM_OFFSET)
 *   0x42100000 ..     TYRIAN_PAK_OFFSET — tyrian.pak lands HERE (~11.4 MB)
 *   0x43fe0000        LiteX video framebuffer region (untouched)
 *
 * The archive itself is standard pakfs format (soc/tools/make_pakfs.py);
 * the directory is parsed here because sdk/pakfs.c only mounts via
 * pak_open(). Follow-up for the SDK: let pak_open()/pakfs_mount() take a
 * destination/limit so big paks work without this bypass.
 *
 * GPL-2.0-or-later (port glue; see compat/SDL.h).
 */
#include "of_files.h"
#include "rvfile.h"

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include <generated/csr.h>
#include <generated/mem.h>
#include <system.h>                     /* flush_cpu_dcache_range */

#include "hal.h"                        /* sys_ticks_us, sys_delay_us */

#define TYRIAN_PAK_OFFSET 0x02100000u   /* byte offset into main_ram */
#define PAK_CHUNK         65536u
#define PAKFS_MAGIC       0x464B4150u   /* "PAKF" */
#define PAKFS_NAME_MAX    47

typedef struct {
	char     name[48];
	uint32_t offset;
	uint32_t size;
} pak_entry_t;

static const uint8_t     *g_pak;
static uint32_t           g_pak_size;
static const pak_entry_t *g_entries;
static uint32_t           g_count;

/* ── Pak slot pull (mirrors hal.c pak_load_slot, custom destination) ── */

static int pak_pull_chunk(uint32_t dst_off, uint32_t offset, uint32_t length)
{
	main_pak_dst_write(dst_off);
	main_pak_offset_write(offset);
	main_pak_length_write(length);
	main_pak_req_write(!main_pak_req_read());       /* toggle = issue */
	uint32_t t0 = sys_ticks_us();
	while (!main_pak_busy_read() && (sys_ticks_us() - t0) < 10000)
		;
	while (main_pak_busy_read() && (sys_ticks_us() - t0) < 2000000)
		;
	if (main_pak_busy_read())
		return -1;
	return main_pak_err_read() ? -1 : 0;
}

static int pak_pull_all(uint32_t *out_size)
{
	/* Pak slot: id 1, datatable word 3 (see hal.c pak_open) */
	main_pak_id_write(1);
	main_pak_dtaddr_write(3);
	sys_delay_us(100);                              /* selector settle */

	uint32_t size = 0;
	for (int i = 0; i < 50 && (size = main_pak_size_read()) == 0; i++)
		sys_delay_us(20000);                        /* wait for user pick */
	if (size == 0)
		return -1;

	uint32_t usable = (size > 2) ? size - 2 : 0;    /* APF EOF wedge */
	for (uint32_t off = 0; off < usable; ) {
		uint32_t chunk = usable - off;
		if (chunk > PAK_CHUNK)
			chunk = PAK_CHUNK;
		if (pak_pull_chunk(TYRIAN_PAK_OFFSET + off, off, chunk) != 0)
			return -2;
		off += chunk;
	}
	flush_cpu_dcache_range((void *)(MAIN_RAM_BASE + TYRIAN_PAK_OFFSET), usable);
	*out_size = usable;
	return 0;
}

/* ── Name handling (game asks in mixed case; pak stores lowercase) ── */

static void lc(char *d, const char *s, size_t n)
{
	size_t i = 0;
	for (; s[i] && i < n - 1; i++)
		d[i] = (s[i] >= 'A' && s[i] <= 'Z') ? (char)(s[i] + 32) : s[i];
	d[i] = 0;
}

static const char *basename_of(const char *p)
{
	const char *b = p;
	for (const char *q = p; *q; q++)
		if (*q == '/' || *q == '\\')
			b = q + 1;
	return b;
}

void of_files_init(void)
{
	uint32_t size = 0;
	int r = pak_pull_all(&size);
	if (r != 0) {
		printf("of_files: pak load failed (%d) — no tyrian.pak picked?\n", r);
		return;                         /* dir_fopen_die will halt with text */
	}

	const uint8_t  *base = (const uint8_t *)(MAIN_RAM_BASE + TYRIAN_PAK_OFFSET);
	const uint32_t *h    = (const uint32_t *)base;
	if (size < 16 || h[0] != PAKFS_MAGIC || h[1] != 1) {
		printf("of_files: not a pakfs archive\n");
		return;
	}
	uint32_t n = h[2];
	if (16 + n * sizeof(pak_entry_t) > size) {
		printf("of_files: truncated pak directory\n");
		return;
	}
	g_pak      = base;
	g_pak_size = size;
	g_entries  = (const pak_entry_t *)(base + 16);
	g_count    = n;
	printf("of_files: tyrian.pak mounted, %lu files, %lu bytes\n",
	       (unsigned long)n, (unsigned long)size);
}

FILE *of_pak_open(const char *name)
{
	if (!g_pak)
		return NULL;

	char want[PAKFS_NAME_MAX + 1];
	lc(want, basename_of(name), sizeof(want));

	for (uint32_t i = 0; i < g_count; i++) {
		if (strcmp(want, g_entries[i].name) == 0) {
			uint32_t off = g_entries[i].offset, size = g_entries[i].size;
			if ((uint64_t)off + size <= g_pak_size)
				return rvfs_open_mem(g_pak + off, size);
			return NULL;
		}
	}
	return NULL;
}

FILE *of_user_open(const char *name, const char *mode)
{
	/* Config/saves stage into session RAM files (see rvfile.c). Hooking
	 * these to the HAL's save_open/save_commit is the noted follow-up. */
	return rvfs_open_user(basename_of(name), mode);
}
