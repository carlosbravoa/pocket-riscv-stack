/*
 * id_fs_rvstack.c — Omnispeak's Filesystem Manager (id_fs.h) on the
 * riscv-stack HAL. Replaces src/id_fs.c entirely (that file needs dirent,
 * fopen and vfprintf — none of which the console has).
 *
 * Three search paths, mapped to platform services:
 *  - Keen data  (GAMEMAPS/EGAGRAPH/AUDIO.CK4)                -> pakfs (read-only,
 *  - Omnispeak data (ACTION/EGAHEAD/... .CK4)                   zero-copy DRAM)
 *  - User data  (OMNISPK.CFG, CONFIG.CK4, SAVEGAM?.CK4)      -> named RAM files,
 *       a subset persisted through save_open/save_commit (32 KB window)
 *
 * FS_File is FILE* in id_fs.h; the handles here are RVF* in disguise —
 * nothing ever passes them to real stdio.
 *
 * Persistence layout (one HAL save region per persisted file):
 *   [u32 magic 'K','E','E','N'] [u32 length] [data ... up to cap-8]
 *
 * Part of the Omnispeak riscv-stack port glue. SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hal.h"    /* FIRST (trap #2: before anything that #defines) */
#include "pakfs.h"
#include "rv_keen.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ck_config.h"
#include "ck_cross.h"
#include "ck_ep.h"
#include "id_fs.h"
#include "id_mm.h"

/* ------------------------------------------------------------ handles -- */

#define RVF_PAK 1
#define RVF_USER 2

typedef struct RVUserFile
{
	char name[16];       /* uppercased, 8.3 */
	uint8_t *data;
	uint32_t size;       /* current logical length */
	uint32_t cap;        /* allocated */
	int dirty;
	save_file_t *save;   /* non-NULL = persisted region */
} RVUserFile;

typedef struct RVF
{
	int kind;
	pakfs_file_t pf;     /* RVF_PAK */
	RVUserFile *uf;      /* RVF_USER */
	uint32_t pos;        /* RVF_USER read/write position */
	int writing;
} RVF;

#define RV_MAX_OPEN 12
static RVF rvf_pool[RV_MAX_OPEN];

#define RV_MAX_USER 10
static RVUserFile rv_user[RV_MAX_USER];

#define RV_SAVE_MAGIC 0x4E45454Bu /* 'KEEN' LE */

/* The persisted subset and its budget: all regions share the console's
 * 32 KB per-game save window (TOC included). See PORTING.md for the math. */
static save_file_t rv_save_cfg;   /* OMNISPK.CFG  cap 2 KB  */
static save_file_t rv_save_conf;  /* CONFIG.CK4   cap 2 KB  */
static save_file_t rv_save_sg0;   /* SAVEGAM0.CK4 cap 24 KB */

static struct
{
	const char *name;
	save_file_t *sf;
	uint32_t cap;
} rv_persist[] = {
	{"OMNISPK.CFG", &rv_save_cfg, 2048},
	{"CONFIG.CK4", &rv_save_conf, 2048},
	{"SAVEGAM0.CK4", &rv_save_sg0, 24576},
};
#define RV_NPERSIST (int)(sizeof(rv_persist) / sizeof(rv_persist[0]))

static int rv_platform_up;

/* ------------------------------------------------------------- helpers -- */

static void rv_upcase(char *dst, const char *src, int cap)
{
	int i;
	for (i = 0; src[i] && i < cap - 1; i++)
		dst[i] = (src[i] >= 'a' && src[i] <= 'z') ? src[i] - 32 : src[i];
	dst[i] = '\0';
}

static RVF *rvf_alloc(void)
{
	for (int i = 0; i < RV_MAX_OPEN; i++)
		if (!rvf_pool[i].kind)
			return &rvf_pool[i];
	return 0;
}

static RVUserFile *rv_user_find(const char *upname)
{
	for (int i = 0; i < RV_MAX_USER; i++)
		if (rv_user[i].name[0] && !strcmp(rv_user[i].name, upname))
			return &rv_user[i];
	return 0;
}

static RVUserFile *rv_user_create(const char *upname)
{
	RVUserFile *uf = rv_user_find(upname);
	if (uf)
		return uf;
	for (int i = 0; i < RV_MAX_USER; i++)
		if (!rv_user[i].name[0])
		{
			uf = &rv_user[i];
			memset(uf, 0, sizeof(*uf));
			strcpy(uf->name, upname);
			return uf;
		}
	return 0;
}

static void rv_user_grow(RVUserFile *uf, uint32_t need)
{
	if (need <= uf->cap)
		return;
	uint32_t ncap = uf->cap ? uf->cap : 1024;
	while (ncap < need)
		ncap *= 2;
	uint8_t *nd = (uint8_t *)malloc(ncap);
	if (uf->data)
	{
		memcpy(nd, uf->data, uf->size);
		free(uf->data);
	}
	uf->data = nd;
	uf->cap = ncap;
}

/* Persist one user file into its HAL save region and commit it. */
static void rv_user_persist(RVUserFile *uf)
{
	if (!uf->save || !uf->dirty)
		return;
	uint8_t *base = (uint8_t *)uf->save->base;
	uint32_t room = uf->save->size - 8;
	uint32_t len = uf->size;
	if (len > room)
	{
		CK_Cross_LogMessage(CK_LOG_MSG_ERROR,
			"rvfs: %s (%d B) exceeds its save budget (%d B); truncating!\n",
			uf->name, (int)len, (int)room);
		len = room;
	}
	uint32_t magic = RV_SAVE_MAGIC;
	memcpy(base, &magic, 4);
	memcpy(base + 4, &len, 4);
	memcpy(base + 8, uf->data, len);
	if (save_commit(uf->save) == 0)
		uf->dirty = 0;
}

void RVK_FS_FlushAll(void)
{
	for (int i = 0; i < RV_MAX_USER; i++)
		if (rv_user[i].name[0])
			rv_user_persist(&rv_user[i]);
}

/* ------------------------------------------------- platform bring-up -- */

void RVK_PlatformInit(void)
{
	if (rv_platform_up)
		return;
	rv_platform_up = 1;

	sys_init();

	int r = pakfs_mount();
	if (r != 0)
		CK_Cross_LogMessage(CK_LOG_MSG_ERROR,
			"rvfs: pakfs_mount failed (%d) — no data files!\n", r);

	/* Restore the persisted user files (save_open is costly: boot only). */
	for (int i = 0; i < RV_NPERSIST; i++)
	{
		int sr = save_open(rv_persist[i].name[0] == 'S' ? "savegam0"
				: (rv_persist[i].name[0] == 'O' ? "omnispk_cfg" : "config"),
			rv_persist[i].cap, rv_persist[i].sf);
		if (sr < 0)
		{
			CK_Cross_LogMessage(CK_LOG_MSG_WARNING,
				"rvfs: save_open(%s) failed (%d): running without persistence\n",
				rv_persist[i].name, sr);
			continue;
		}
		RVUserFile *uf = rv_user_create(rv_persist[i].name);
		if (!uf)
			continue;
		uf->save = rv_persist[i].sf;
		if (sr == 0) /* previous content restored: unpack if it's ours */
		{
			uint8_t *base = (uint8_t *)uf->save->base;
			uint32_t magic, len;
			memcpy(&magic, base, 4);
			memcpy(&len, base + 4, 4);
			if (magic == RV_SAVE_MAGIC && len <= uf->save->size - 8)
			{
				rv_user_grow(uf, len);
				memcpy(uf->data, base + 8, len);
				uf->size = len;
			}
		}
	}

	RVK_Beacon(1);
}

void RVK_Beacon(int stage)
{
	sys_diag(0xBEAC0000u | (unsigned)stage);
	/* Paint a coarse progress bar straight into the framebuffer: a photo
	 * of a wedged boot tells us which stage died. Bright rgb332-ish
	 * values so it reads under the reset palette too. */
	uint8_t *fb = fb_backbuffer();
	if (!fb)
		return;
	int w = fb_width();
	static const uint8_t c[5] = {0xFF, 0xE0, 0xFC, 0x1C, 0x03};
	for (int y = 0; y < 4; y++)
		for (int x = 0; x < 10 * 8; x++)
			fb[y * w + x] = (x / 16 < stage) ? c[(x / 16) % 5] : 0;
	fb_present();
}

void RVK_QuitToPicker(void)
{
	RVK_FS_FlushAll();
	sys_exit();
}

/* --------------------------------------------------------- id_fs API -- */

bool FS_IsFileValid(FS_File file)
{
	return (file != 0);
}

size_t FS_Read(void *ptr, size_t size, size_t nmemb, FS_File file)
{
	RVF *f = (RVF *)file;
	if (!f)
		return 0;
	if (f->kind == RVF_PAK)
		return pakfs_read(ptr, size, nmemb, &f->pf);
	/* user file */
	if (!size || !nmemb)
		return 0;
	uint32_t want = (uint32_t)(size * nmemb);
	uint32_t have = (f->pos < f->uf->size) ? f->uf->size - f->pos : 0;
	if (want > have)
		want = have - (have % size);
	memcpy(ptr, f->uf->data + f->pos, want);
	f->pos += want;
	return want / size;
}

size_t FS_Write(const void *ptr, size_t size, size_t nmemb, FS_File file)
{
	RVF *f = (RVF *)file;
	if (!f || f->kind != RVF_USER)
		return 0;
	uint32_t want = (uint32_t)(size * nmemb);
	rv_user_grow(f->uf, f->pos + want);
	memcpy(f->uf->data + f->pos, ptr, want);
	f->pos += want;
	if (f->pos > f->uf->size)
		f->uf->size = f->pos;
	f->uf->dirty = 1;
	return nmemb;
}

size_t FS_SeekTo(FS_File file, size_t offset)
{
	RVF *f = (RVF *)file;
	if (!f)
		return 0;
	if (f->kind == RVF_PAK)
	{
		size_t old = pakfs_tell(&f->pf);
		pakfs_seek(&f->pf, (int32_t)offset, PAKFS_SEEK_SET);
		return old;
	}
	size_t old = f->pos;
	f->pos = (uint32_t)offset;
	if (f->pos > f->uf->size) /* no sparse files needed */
		f->pos = f->uf->size;
	return old;
}

void FS_CloseFile(FS_File file)
{
	RVF *f = (RVF *)file;
	if (!f)
		return;
	if (f->kind == RVF_USER && f->writing)
		rv_user_persist(f->uf);
	memset(f, 0, sizeof(*f));
}

size_t FS_GetFileSize(FS_File file)
{
	RVF *f = (RVF *)file;
	if (!f)
		return 0;
	return (f->kind == RVF_PAK) ? f->pf.size : f->uf->size;
}

/* Keen and Omnispeak data both live in the single pak (case-folded). */
static FS_File rv_open_pak(const char *fileName)
{
	char up[PAKFS_NAME_MAX + 1];
	rv_upcase(up, fileName, sizeof(up));
	RVF *f = rvf_alloc();
	if (!f)
		return 0;
	if (pakfs_open(up, &f->pf) != 0)
		return 0;
	f->kind = RVF_PAK;
	return (FS_File)f;
}

FS_File FS_OpenKeenFile(const char *fileName)
{
	return rv_open_pak(fileName);
}

FS_File FS_OpenOmniFile(const char *fileName)
{
	return rv_open_pak(fileName);
}

FS_File FS_OpenUserFile(const char *fileName)
{
	char up[16];
	rv_upcase(up, fileName, sizeof(up));
	RVUserFile *uf = rv_user_find(up);
	if (!uf || (!uf->size && !uf->dirty))
		return 0; /* never written = not present */
	RVF *f = rvf_alloc();
	if (!f)
		return 0;
	f->kind = RVF_USER;
	f->uf = uf;
	f->pos = 0;
	f->writing = 0;
	return (FS_File)f;
}

FS_File FS_CreateUserFile(const char *fileName)
{
	char up[16];
	rv_upcase(up, fileName, sizeof(up));
	RVUserFile *uf = rv_user_create(up);
	if (!uf)
		return 0;
	uf->size = 0; /* truncate */
	uf->dirty = 1;
	RVF *f = rvf_alloc();
	if (!f)
		return 0;
	f->kind = RVF_USER;
	f->uf = uf;
	f->pos = 0;
	f->writing = 1;
	return (FS_File)f;
}

/* Adjusts the extension on a filename to match the current episode.
 * (Same contract as upstream: static buffer, not thread safe.) */
char *FS_AdjustExtension(const char *filename)
{
	static char newname[FS_MAX_FILENAME_LEN];
	size_t fnamelen = CK_Cross_strscpy(newname, filename, sizeof(newname));
	newname[fnamelen - 3] = ck_currentEpisode->ext[0];
	newname[fnamelen - 2] = ck_currentEpisode->ext[1];
	newname[fnamelen - 1] = ck_currentEpisode->ext[2];
	return newname;
}

bool FS_IsKeenFilePresent(const char *filename)
{
	FS_File f = FS_OpenKeenFile(filename);
	if (!FS_IsFileValid(f))
		return false;
	FS_CloseFile(f);
	return true;
}

bool FS_IsOmniFilePresent(const char *filename)
{
	FS_File f = FS_OpenOmniFile(filename);
	if (!FS_IsFileValid(f))
		return false;
	FS_CloseFile(f);
	return true;
}

bool FS_IsUserFilePresent(const char *filename)
{
	FS_File f = FS_OpenUserFile(filename);
	if (!FS_IsFileValid(f))
		return false;
	FS_CloseFile(f);
	return true;
}

void FS_Startup()
{
	/* The first call Omnispeak's main() makes: bring the platform up. */
	RVK_PlatformInit();
	CK_Cross_LogMessage(CK_LOG_MSG_NORMAL,
		"FS_Startup: keen+omni data = pak, user data = HAL saves\n");
}

size_t FS_ReadInt8LE(void *ptr, size_t count, FS_File stream)
{
	return FS_Read(ptr, 1, count, stream);
}

size_t FS_ReadInt16LE(void *ptr, size_t count, FS_File stream)
{
	return FS_Read(ptr, 2, count, stream); /* LE target; no swap */
}

size_t FS_ReadInt32LE(void *ptr, size_t count, FS_File stream)
{
	return FS_Read(ptr, 4, count, stream);
}

size_t FS_WriteInt8LE(const void *ptr, size_t count, FS_File stream)
{
	return FS_Write(ptr, 1, count, stream);
}

size_t FS_WriteInt16LE(const void *ptr, size_t count, FS_File stream)
{
	return FS_Write(ptr, 2, count, stream);
}

size_t FS_WriteInt32LE(const void *ptr, size_t count, FS_File stream)
{
	return FS_Write(ptr, 4, count, stream);
}

size_t FS_ReadBoolFrom16LE(void *ptr, size_t count, FS_File stream)
{
	uint16_t val;
	size_t actualCount = 0;
	bool *currBoolPtr = (bool *)ptr;
	for (size_t loopVar = 0; loopVar < count; loopVar++, currBoolPtr++)
	{
		if (FS_Read(&val, 2, 1, stream))
		{
			*currBoolPtr = (val != 0);
			actualCount++;
		}
	}
	return actualCount;
}

size_t FS_WriteBoolTo16LE(const void *ptr, size_t count, FS_File stream)
{
	uint16_t val;
	size_t actualCount = 0;
	bool *currBoolPtr = (bool *)ptr;
	for (size_t loopVar = 0; loopVar < count; loopVar++, currBoolPtr++)
	{
		val = (*currBoolPtr) ? 1 : 0;
		actualCount += FS_Write(&val, 2, 1, stream);
	}
	return actualCount;
}

int FS_PrintF(FS_File stream, const char *fmt, ...)
{
	char buf[512];
	va_list args;
	va_start(args, fmt);
	int n = vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	if (n < 0)
		return n;
	if (n > (int)sizeof(buf) - 1)
		n = (int)sizeof(buf) - 1;
	return (int)FS_Write(buf, 1, n, stream) ;
}

bool FS_LoadUserFile(const char *filename, mm_ptr_t *ptr, int *memsize)
{
	FS_File f = FS_OpenUserFile(filename);

	if (!FS_IsFileValid(f))
	{
		*ptr = 0;
		if (memsize)
			*memsize = 0;
		return false;
	}

	int length = (int)FS_GetFileSize(f);
	MM_GetPtr(ptr, length);
	if (memsize)
		*memsize = length;
	int amountRead = (int)FS_Read(*ptr, 1, length, f);
	FS_CloseFile(f);

	return (amountRead == length);
}

#ifndef RVSTACK_PC
/* remove() — id_us_2's save-slot overwrite path. Console: truncate the
 * named RAM file (the following FS_CreateUserFile rewrites it anyway). */
int remove(const char *path)
{
	char up[16];
	rv_upcase(up, path, sizeof(up));
	RVUserFile *uf = rv_user_find(up);
	if (!uf)
		return -1;
	uf->size = 0;
	uf->dirty = 1;
	return 0;
}
#endif
