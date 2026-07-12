/*
 * rvfile.c — FILE-stream shim for the Wolf4SDL riscv-stack port.
 *
 * Wolf does classic stdio on ~8 data files plus config/savegames; the
 * compat/stdio.h shadow header routes every stream call here:
 *
 *   fopen("...", "r*")  ->  1) the pakfs archive (zero-copy DRAM window)
 *                           2) else a named session-RAM file (savegames)
 *   fopen("...", "w*")  ->  named grow-on-demand RAM file; slots listed
 *                           in upersist[] ride the HAL per-game save
 *                           (32 KB window) and survive power-off.
 *
 * Adapted from sdk/tyrian/compat/rvfile.c; the pak side uses the SDK's
 * pakfs module directly (wolf3d.pak is ~1.5 MB — it fits the HAL's
 * default 3 MB pak window, no tyrian-style relocation needed).
 *
 * Part of the Wolf4SDL riscv-stack port glue (see compat/SDL.h).
 */
#include "hal.h"                 /* save_open/commit (persistence) */
#include "pakfs.h"
#include "rvfile.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* the shadow header renamed these to rvfs_*; we're implementing them */
#undef fopen
#undef fclose
#undef fread
#undef fwrite
#undef fseek
#undef ftell
#undef rewind
#undef fgetc
#undef getc
#undef fputc
#undef putc
#undef feof
#undef ferror
#undef fputs
#undef fprintf
#undef fflush
#undef putchar
#undef remove
#undef unlink

enum { RV_MEM_RO = 1, RV_RAM_RW = 2 };

typedef struct ramfile {
	char     name[40];
	uint8_t *buf;
	uint32_t size;                      /* valid bytes */
	uint32_t cap;
	int      used;
	int      restored;                  /* per-game save pulled already */
} ramfile_t;

typedef struct rvfile {
	int            kind;
	const uint8_t *ro;                  /* RV_MEM_RO backing */
	ramfile_t     *rf;                  /* RV_RAM_RW backing */
	uint32_t       pos;
	uint32_t       size;                /* RV_MEM_RO size (RAM: rf->size) */
	int            eof, err, writable;
} rvfile_t;

#define RV_MAX_RAMFILES 14
static ramfile_t ramfiles[RV_MAX_RAMFILES];

static void rvfs_user_restore(ramfile_t *rf);

static int is_console(FILE *f)
{
	return f == RVFS_STDOUT || f == RVFS_STDERR || f == RVFS_STDIN;
}

static rvfile_t *rv(FILE *f) { return (rvfile_t *)f; }

/* ── Name handling ─────────────────────────────────────────────────────── */

static void lc_base(char *d, const char *path, size_t n)
{
	const char *b = path;
	for (const char *q = path; *q; q++)
		if (*q == '/' || *q == '\\')
			b = q + 1;
	size_t i = 0;
	for (; b[i] && i < n - 1; i++)
		d[i] = (b[i] >= 'A' && b[i] <= 'Z') ? (char)(b[i] + 32) : b[i];
	d[i] = 0;
}

/* ── Pak side (reads) ──────────────────────────────────────────────────── */

static int pak_state;                   /* 0 unknown, 1 ok, -1 failed */

int rvfs_pak_ready(void)
{
	if (pak_state == 0)
		pak_state = (pakfs_mount() == 0) ? 1 : -1;
	return pak_state == 1 ? 0 : -1;
}

const void *rvfs_pak_data(const char *name, uint32_t *size_out)
{
	if (rvfs_pak_ready() != 0)
		return NULL;
	char want[PAKFS_NAME_MAX + 1];
	lc_base(want, name, sizeof(want));
	return pakfs_data(want, size_out);
}

/* ── RAM files (writes: config + savegames) ────────────────────────────── */

static ramfile_t *ram_lookup(const char *name, int create)
{
	for (int i = 0; i < RV_MAX_RAMFILES; i++)
		if (ramfiles[i].used && strcmp(ramfiles[i].name, name) == 0)
			return &ramfiles[i];
	if (!create)
		return NULL;
	for (int i = 0; i < RV_MAX_RAMFILES; i++) {
		if (!ramfiles[i].used) {
			ramfiles[i].used = 1;
			strncpy(ramfiles[i].name, name, sizeof(ramfiles[i].name) - 1);
			ramfiles[i].name[sizeof(ramfiles[i].name) - 1] = 0;
			ramfiles[i].size = 0;
			return &ramfiles[i];
		}
	}
	return NULL;
}

/* ── Persistence: RAM-files ride the HAL's per-game save ───────────────────
 * Fixed capacities keyed by name (save_open capacities are immutable); all
 * of a game's regions share the 32 KB save window. A full Wolf3D savegame
 * (~20-45 KB: tilemap + actorat + objects) does NOT fit alongside config,
 * so ONE savegame slot (savegam0) is persisted with the rest of the window
 * and the others stay session-RAM — see PORTING.md for the layout math. */
typedef struct { const char *name; uint32_t cap; } upersist_t;
static const upersist_t upersist[] = {
	{ "config.wl1",   1536 },  { "config.wl6",   1536 },
	{ "savegam0.wl1", 26624 }, { "savegam0.wl6", 26624 },
};

static const upersist_t *persist_slot(const char *name)
{
	for (unsigned i = 0; i < sizeof(upersist) / sizeof(*upersist); i++)
		if (strcmp(upersist[i].name, name) == 0)
			return &upersist[i];
	return NULL;
}

/* first 4 bytes of the region = stored size, data follows */
static void rvfs_user_restore(ramfile_t *rf)
{
	const upersist_t *ps = persist_slot(rf->name);
	if (!ps || rf->restored)
		return;
	rf->restored = 1;
	save_file_t sf;
	if (save_open(rf->name, ps->cap + 4, &sf) != 0)
		return;                         /* fresh (r==1) or none: keep empty */
	uint32_t stored = *(const uint32_t *)(uintptr_t)sf.base;
	if (stored == 0 || stored > ps->cap)
		return;
	if (rf->cap < stored) {
		uint8_t *nb = realloc(rf->buf, stored);
		if (!nb)
			return;
		rf->buf = nb;
		rf->cap = stored;
	}
	memcpy(rf->buf, (const void *)(uintptr_t)(sf.base + 4), stored);
	rf->size = stored;
	printf("rvfile: restored '%s' (%lu bytes)\n", rf->name,
	       (unsigned long)stored);
}

static void user_persist(ramfile_t *rf)
{
	const upersist_t *ps = persist_slot(rf->name);
	if (!ps || rf->size > ps->cap)
		return;
	save_file_t sf;
	if (save_open(rf->name, ps->cap + 4, &sf) < 0)
		return;
	*(uint32_t *)(uintptr_t)sf.base = rf->size;
	memcpy((void *)(uintptr_t)(sf.base + 4), rf->buf, rf->size);
	save_commit(&sf);
}

/* ── open/close ────────────────────────────────────────────────────────── */

static FILE *open_mem(const void *data, uint32_t size)
{
	rvfile_t *f = calloc(1, sizeof(*f));
	if (!f)
		return NULL;
	f->kind = RV_MEM_RO;
	f->ro   = data;
	f->size = size;
	return (FILE *)f;
}

static FILE *open_user(const char *name, const char *mode)
{
	int want_write = (mode[0] == 'w' || mode[0] == 'a' ||
	                  (mode[0] == 'r' && strchr(mode, '+')));

	ramfile_t *rf = ram_lookup(name, 1);
	if (!rf)
		return NULL;
	rvfs_user_restore(rf);              /* lazy pull from the per-game save */
	if (mode[0] == 'r' && rf->size == 0 && !strchr(mode, '+'))
		return NULL;                    /* "no such file" until first write */

	rvfile_t *f = calloc(1, sizeof(*f));
	if (!f)
		return NULL;
	f->kind     = RV_RAM_RW;
	f->rf       = rf;
	f->writable = want_write;
	if (mode[0] == 'w')
		rf->size = 0;                   /* truncate */
	f->pos = (mode[0] == 'a') ? rf->size : 0;
	return (FILE *)f;
}

FILE *rvfs_fopen(const char *path, const char *mode)
{
	char name[40];
	lc_base(name, path, sizeof(name));

	if (mode[0] == 'r' && !strchr(mode, '+')) {
		uint32_t size = 0;
		const void *data = rvfs_pak_data(name, &size);
		if (data)
			return open_mem(data, size);
	}
	return open_user(name, mode);
}

int rvfs_remove(const char *path)
{
	char name[40];
	lc_base(name, path, sizeof(name));
	ramfile_t *rf = ram_lookup(name, 0);
	if (!rf) {
		/* still zero a persisted-but-unopened slot */
		rf = persist_slot(name) ? ram_lookup(name, 1) : NULL;
		if (!rf)
			return -1;
		rf->restored = 1;               /* don't resurrect it from the save */
	}
	rf->size = 0;
	user_persist(rf);                   /* stored size 0 = deleted */
	return 0;
}

int rvfs_fclose(FILE *f)
{
	if (!f || is_console(f))
		return 0;
	rvfile_t *rf = rv(f);
	if (rf->kind == RV_RAM_RW && rf->writable && rf->rf)
		user_persist(rf->rf);           /* write-back on close */
	free(rf);
	return 0;
}

/* ── stream ops ────────────────────────────────────────────────────────── */

static uint32_t rv_size(rvfile_t *f)
{
	return f->kind == RV_RAM_RW ? f->rf->size : f->size;
}

size_t rvfs_fread(void *dst, size_t size, size_t n, FILE *fp)
{
	if (!fp || is_console(fp) || !size)
		return 0;
	rvfile_t *f = rv(fp);
	uint32_t total = rv_size(f);
	uint32_t avail = (f->pos < total) ? total - f->pos : 0;
	uint32_t want  = (uint32_t)(size * n);
	if (want > avail) {
		want   = avail - (avail % (uint32_t)size);  /* whole elements */
		f->eof = 1;
	}
	const uint8_t *src = (f->kind == RV_RAM_RW) ? f->rf->buf : f->ro;
	memcpy(dst, src + f->pos, want);
	f->pos += want;
	return want / size;
}

static int ram_ensure(ramfile_t *rf, uint32_t need)
{
	if (need <= rf->cap)
		return 0;
	uint32_t cap = rf->cap ? rf->cap : 4096;
	while (cap < need)
		cap *= 2;
	uint8_t *nb = realloc(rf->buf, cap);
	if (!nb)
		return -1;
	rf->buf = nb;
	rf->cap = cap;
	return 0;
}

size_t rvfs_fwrite(const void *src, size_t size, size_t n, FILE *fp)
{
	if (!fp || !size)
		return 0;
	if (is_console(fp)) {
		const char *s = src;
		for (size_t i = 0; i < size * n; i++)
			printf("%c", s[i]);
		return n;
	}
	rvfile_t *f = rv(fp);
	if (f->kind != RV_RAM_RW || !f->writable) {
		f->err = 1;
		return 0;
	}
	uint32_t want = (uint32_t)(size * n);
	if (ram_ensure(f->rf, f->pos + want) != 0) {
		f->err = 1;
		return 0;
	}
	memcpy(f->rf->buf + f->pos, src, want);
	f->pos += want;
	if (f->pos > f->rf->size)
		f->rf->size = f->pos;
	return n;
}

int rvfs_fseek(FILE *fp, long off, int whence)
{
	if (!fp || is_console(fp))
		return -1;
	rvfile_t *f = rv(fp);
	long total = (long)rv_size(f);
	long p = (whence == SEEK_SET) ? off
	       : (whence == SEEK_CUR) ? (long)f->pos + off
	       : total + off;
	if (p < 0)
		return -1;
	f->pos = (uint32_t)p;               /* past-EOF seek of a RAM file is
	                                     * fine; next write grows it */
	f->eof = 0;
	return 0;
}

long rvfs_ftell(FILE *fp)
{
	if (!fp || is_console(fp))
		return -1;
	return (long)rv(fp)->pos;
}

void rvfs_rewind(FILE *fp)
{
	(void)rvfs_fseek(fp, 0, SEEK_SET);
}

int rvfs_fgetc(FILE *fp)
{
	unsigned char c;
	return rvfs_fread(&c, 1, 1, fp) == 1 ? c : EOF;
}

int rvfs_fputc(int c, FILE *fp)
{
	unsigned char b = (unsigned char)c;
	return rvfs_fwrite(&b, 1, 1, fp) == 1 ? b : EOF;
}

int rvfs_feof(FILE *fp)
{
	if (!fp || is_console(fp))
		return 0;
	return rv(fp)->eof;
}

int rvfs_ferror(FILE *fp)
{
	if (!fp || is_console(fp))
		return 0;
	return rv(fp)->err;
}

int rvfs_fputs(const char *s, FILE *fp)
{
	if (is_console(fp)) {
		printf("%s", s);
		return 0;
	}
	size_t len = strlen(s);
	return rvfs_fwrite(s, 1, len, fp) == len ? 0 : EOF;
}

int rvfs_fflush(FILE *fp)
{
	(void)fp;
	return 0;
}

int rvfs_putchar(int c)
{
	printf("%c", c);
	return c;
}

int rvfs_fprintf(FILE *fp, const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (n < 0)
		return n;
	if (n >= (int)sizeof(buf))
		n = (int)sizeof(buf) - 1;
	if (is_console(fp)) {
		printf("%s", buf);
		return n;
	}
	return (int)rvfs_fwrite(buf, 1, (size_t)n, fp) == n ? n : -1;
}
