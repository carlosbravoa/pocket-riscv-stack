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

#include "hal.h"                        /* pak_open_at (console + PC twin) */

#define TYRIAN_PAK_OFFSET 0x02100000u   /* byte offset into main_ram (console) */
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

/* Pak pull: the official SDK path. On the console pak_open_at() lands the
 * 11.4 MB pak ABOVE the game region (the default 3 MB window sits below the
 * game image and would be overwritten); on the PC twin it reads ./game.pak
 * or $RVSTACK_PAK from disk. Replaces the raw-CSR bypass this file carried
 * before pak_open_at existed. */
static int pak_pull_all(uint32_t *out_size)
{
#if 0 /* DISABLED on hardware: DRAM refresh stops between core loads, so a
       * stale pak passes the header check with decayed data — hung menus and
       * silent audio (field v0.19.0). Safe only for the sim backdoor. */
	/* Warm pak: if a valid pakfs image is ALREADY at the landing address
	 * (soft reboot, game re-pick — DRAM survives; or the sim's backdoor
	 * preload), skip the multi-second pull entirely. Console-only: on the
	 * PC twin this raw address doesn't exist. */
	const uint8_t  *base = (const uint8_t *)(0x40000000u + TYRIAN_PAK_OFFSET);
	const uint32_t *h    = (const uint32_t *)base;
	if (h[0] == PAKFS_MAGIC && h[1] == 1 && h[2] > 0 && h[2] <= 512) {
		const pak_entry_t *e = (const pak_entry_t *)(base + 16);
		uint32_t top = 16 + h[2] * sizeof(pak_entry_t);
		for (uint32_t i = 0; i < h[2]; i++)
			if (e[i].offset + e[i].size > top)
				top = e[i].offset + e[i].size;
		if (top < 64u << 20) {
			g_pak     = base;
			*out_size = top;
			printf("of_files: warm pak reused (%lu bytes)\n",
			       (unsigned long)top);
			return 0;
		}
	}
#endif
	pak_file_t p;
	if (pak_open_at(TYRIAN_PAK_OFFSET, &p) != 0)
		return -1;
	g_pak     = (const uint8_t *)p.base;
	*out_size = p.size;
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

	const uint8_t  *base = g_pak;
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
