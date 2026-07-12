#include "hiscore.h"

/* RVSTACK: hal.h first (include-order trap, PORTABILITY.md #2) */
#include "hal.h"
#include <stdint.h>
#include <string.h>

#include "logsys.h"

static HiScore table[HISCORE_MAX];
static int count = 0;

/* RVSTACK: upstream persisted "highscores.txt" with fopen/fgets/fprintf.
 * The console has no writable filesystem — the table now lives in the HAL
 * save API (hal.h): the Pocket keeps it in Saves/riscv_stack/<game>.sav,
 * the PC twin in a local .sav file. Budget: the blob below is
 * 12 + 5*28 = 152 bytes of the 32 KB per-game save window — nothing.
 * save_open once at boot (it is costly), save_commit on new records only. */
#define HS_MAGIC   0x4B524251u          /* 'QBRK' */
#define HS_VERSION 1u

typedef struct {
	uint32_t magic, version, count;
	struct {
		char    name[HISCORE_NAME];
		int32_t score, level, lines;
	} e[HISCORE_MAX];
} hs_blob_t;

static save_file_t hs_file;
static int hs_ok = 0;

// Copy at most HISCORE_NAME-1 chars and null-terminate; blank names become "---"
static void set_name(char *dst, const char *src) {
	if(!src || src[0] == '\0') src = "---";
	int n = 0;
	for(; src[n] && n < HISCORE_NAME - 1; n++) dst[n] = src[n];
	dst[n] = '\0';
}

void hiscore_load(const char *path) {
	(void)path;                         /* RVSTACK: named save, not a file */
	count = 0;
	int r = save_open("hiscores", sizeof(hs_blob_t), &hs_file);
	if(r < 0) {
		log_msgf(WARNING, "hiscore: no save backend, table is session-only\n");
		return;                         /* run without persistence */
	}
	hs_ok = 1;
	if(r != 0) return;                  /* created fresh: empty table */
	hs_blob_t blob;
	memcpy(&blob, (const void *)hs_file.base, sizeof(blob));
	if(blob.magic != HS_MAGIC || blob.version != HS_VERSION) return;
	if(blob.count > HISCORE_MAX) blob.count = HISCORE_MAX;
	for(uint32_t i = 0; i < blob.count; i++) {
		set_name(table[i].name, blob.e[i].name);
		table[i].score = blob.e[i].score;
		table[i].level = blob.e[i].level;
		table[i].lines = blob.e[i].lines;
	}
	count = (int)blob.count;
}

void hiscore_save(const char *path) {
	(void)path;                         /* RVSTACK: named save, not a file */
	if(!hs_ok) return;
	hs_blob_t blob;
	memset(&blob, 0, sizeof(blob));
	blob.magic = HS_MAGIC;
	blob.version = HS_VERSION;
	blob.count = (uint32_t)count;
	for(int i = 0; i < count; i++) {
		set_name(blob.e[i].name, table[i].name);
		blob.e[i].score = table[i].score;
		blob.e[i].level = table[i].level;
		blob.e[i].lines = table[i].lines;
	}
	memcpy((void *)hs_file.base, &blob, sizeof(blob));
	if(save_commit(&hs_file) < 0)
		log_msgf(ERROR, "hiscore: save_commit failed\n");
}

int hiscore_qualifies(int score) {
	if(score <= 0) return 0;
	if(count < HISCORE_MAX) return 1;
	return score > table[HISCORE_MAX - 1].score;
}

int hiscore_insert(const char *name, int score, int level, int lines) {
	// Find insertion rank (entries sorted high-to-low)
	int rank = count;
	for(int i = 0; i < count; i++) {
		if(score > table[i].score) { rank = i; break; }
	}
	if(rank >= HISCORE_MAX) return -1;
	// Shift lower entries down one slot
	int last = count < HISCORE_MAX ? count : HISCORE_MAX - 1;
	for(int i = last; i > rank; i--) table[i] = table[i - 1];
	set_name(table[rank].name, name);
	table[rank].score = score;
	table[rank].level = level;
	table[rank].lines = lines;
	if(count < HISCORE_MAX) count++;
	return rank;
}

int hiscore_count() { return count; }

const HiScore *hiscore_get(int i) {
	if(i < 0 || i >= count) return NULL;
	return &table[i];
}

int hiscore_best() { return count > 0 ? table[0].score : 0; }
