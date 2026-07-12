#ifndef TETRIS_INPUT
#define TETRIS_INPUT

#include <SDL2/SDL.h>

// A couple structs that contain key/mouse button status
// There is a "current" state and an "old" state (previous frame)
// RVSTACK: upstream DEFINED these in the header (every includer got a copy —
// only links under pre-gcc-10 -fcommon). Declare here, define in input.c.
typedef struct {
	Uint8 up; Uint8 down; Uint8 left; Uint8 right;
	Uint8 z; Uint8 x; Uint8 shift; Uint8 space; Uint8 enter; Uint8 esc;
} keystate_t;
extern keystate_t key, oldKey;

// Updates the input structs to new values, and also handles SDL events
int input_update();

#endif

