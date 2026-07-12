#include "hiscore.h"

#include <stdio.h>
#include <string.h>

#include "logsys.h"

static HiScore table[HISCORE_MAX];
static int count = 0;

// Copy at most HISCORE_NAME-1 chars and null-terminate; blank names become "---"
static void set_name(char *dst, const char *src) {
	if(!src || src[0] == '\0') src = "---";
	int n = 0;
	for(; src[n] && n < HISCORE_NAME - 1; n++) dst[n] = src[n];
	dst[n] = '\0';
}

void hiscore_load(const char *path) {
	count = 0;
	FILE *f = fopen(path, "r");
	if(!f) return;
	// Each line: score level lines name (name may contain spaces, read to EOL)
	char line[128];
	while(count < HISCORE_MAX && fgets(line, sizeof(line), f)) {
		int sc = 0, lv = 0, ln = 0;
		char nm[64] = {0};
		int got = sscanf(line, "%d %d %d %63[^\n]", &sc, &lv, &ln, nm);
		if(got < 3) continue;
		table[count].score = sc;
		table[count].level = lv;
		table[count].lines = ln;
		// strip a single leading space left by the name field
		set_name(table[count].name, got >= 4 ? (nm[0] == ' ' ? nm + 1 : nm) : "");
		count++;
	}
	fclose(f);
}

void hiscore_save(const char *path) {
	FILE *f = fopen(path, "w");
	if(!f) { log_msgf(ERROR, "hiscore_save: cannot open %s\n", path); return; }
	for(int i = 0; i < count; i++) {
		fprintf(f, "%d %d %d %s\n", table[i].score, table[i].level,
			table[i].lines, table[i].name);
	}
	fclose(f);
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
