// pakfs — see pakfs.h. Zero-copy reader over the DRAM-resident pak.
// SPDX-License-Identifier: BSD-2-Clause
#include "pakfs.h"
#include "hal.h"
#include <string.h>

#define PAKFS_MAGIC 0x464B4150u             // "PAKF" little-endian

typedef struct {
	char     name[48];
	uint32_t offset;
	uint32_t size;
} pakfs_entry_t;

static const uint8_t       *pak_base;
static const pakfs_entry_t *pak_dir;
static uint32_t             pak_size;
static int                  pak_n = -1;

int pakfs_mount(void)
{
	pak_n = -1;
	pak_file_t p;
	if (pak_open(NULL, &p) != 0)
		return -1;
	pak_base = (const uint8_t *)p.base;
	pak_size = p.size;
	const uint32_t *h = (const uint32_t *)pak_base;
	if (p.size < 16 || h[0] != PAKFS_MAGIC || h[1] != 1)
		return -2;                          // plain pak: use pak_open() instead
	uint32_t n = h[2];
	if (16 + n * sizeof(pakfs_entry_t) > p.size)
		return -2;                          // truncated directory
	pak_dir = (const pakfs_entry_t *)(pak_base + 16);
	pak_n   = (int)n;
	return 0;
}

int pakfs_nfiles(void) { return pak_n; }

const char *pakfs_name(int i)
{
	return (pak_n < 0 || i < 0 || i >= pak_n) ? 0 : pak_dir[i].name;
}

static const pakfs_entry_t *find(const char *name)
{
	if (pak_n < 0 || !name)
		return 0;
	for (int i = 0; i < pak_n; i++)
		if (!strncmp(pak_dir[i].name, name, PAKFS_NAME_MAX + 1))
			return &pak_dir[i];
	return 0;
}

const void *pakfs_data(const char *name, uint32_t *size_out)
{
	const pakfs_entry_t *e = find(name);
	if (!e || e->offset + e->size > pak_size)
		return 0;
	if (size_out)
		*size_out = e->size;
	return pak_base + e->offset;
}

int pakfs_open(const char *name, pakfs_file_t *f)
{
	uint32_t sz;
	const void *d = pakfs_data(name, &sz);
	if (!d)
		return -1;
	f->base = (const uint8_t *)d;
	f->size = sz;
	f->pos  = 0;
	return 0;
}

uint32_t pakfs_read(void *dst, uint32_t sz, uint32_t n, pakfs_file_t *f)
{
	if (!sz)
		return 0;
	uint32_t want  = sz * n;
	uint32_t avail = f->size - f->pos;
	if (want > avail)
		want = avail - (avail % sz);        // whole elements, like fread
	memcpy(dst, f->base + f->pos, want);
	f->pos += want;
	return want / sz;
}

int pakfs_seek(pakfs_file_t *f, int32_t off, int whence)
{
	int64_t p = (whence == PAKFS_SEEK_SET) ? off
	          : (whence == PAKFS_SEEK_CUR) ? (int64_t)f->pos + off
	          : (int64_t)f->size + off;
	if (p < 0 || p > (int64_t)f->size)
		return -1;
	f->pos = (uint32_t)p;
	return 0;
}

uint32_t pakfs_tell(const pakfs_file_t *f) { return f->pos; }
