#ifndef TETRIS_INPUT
#define TETRIS_INPUT

#include <SDL2/SDL.h>

// A couple structs that contain key/mouse button status
// There is a "current" state and an "old" state (previous frame)
typedef struct {
	Uint8 up; Uint8 down; Uint8 left; Uint8 right;
	Uint8 z; Uint8 x; Uint8 c; Uint8 shift; Uint8 space; Uint8 enter; Uint8 esc;
} KeyState;
extern KeyState key, oldKey;

// Updates the input structs to new values, and also handles SDL events
int input_update();

// --- Optional text entry (e.g. typing a high-score name) ---
// RVSTACK: rebuilt for pad-only input — three arcade initials, cursor +
// character cycling driven from the game-over screen (no keyboard events).
void input_start_text();
void input_stop_text();
// The text captured so far (null-terminated), and its length
const char *input_text();
int input_text_len();
// RVSTACK: selected slot (0..2) while entry is active, -1 otherwise
int input_text_cursor();
// RVSTACK: move the cursor (+1/-1, wraps) / cycle the char at the cursor
void input_text_move(int d);
void input_text_cycle(int d);

#endif
