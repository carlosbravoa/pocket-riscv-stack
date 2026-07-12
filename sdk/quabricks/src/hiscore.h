#ifndef TETRIS_HISCORE
#define TETRIS_HISCORE

// Persistent local high-score table (top N kept, saved to a text file)
#define HISCORE_MAX 5
#define HISCORE_NAME 16

typedef struct {
	char name[HISCORE_NAME];
	int score;
	int level;
	int lines;
} HiScore;

// Load the table from `path` (missing/invalid file just yields an empty table)
void hiscore_load(const char *path);
// Write the table back to `path`
void hiscore_save(const char *path);

// True if `score` would earn a place in the table
int hiscore_qualifies(int score);
// Insert an entry keeping the table sorted/truncated; returns its rank (0-based) or -1
int hiscore_insert(const char *name, int score, int level, int lines);

// Number of entries currently stored
int hiscore_count();
// Entry at rank i (0 = best), or NULL if out of range
const HiScore *hiscore_get(int i);
// Best score, or 0 when the table is empty
int hiscore_best();

#endif
