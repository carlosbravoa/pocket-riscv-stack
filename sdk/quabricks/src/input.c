#include "input.h"

// Definitions for the input state declared extern in input.h
KeyState key, oldKey;

const int K_LEFT   = SDL_SCANCODE_LEFT;
const int K_RIGHT  = SDL_SCANCODE_RIGHT;
const int K_UP     = SDL_SCANCODE_UP;
const int K_DOWN   = SDL_SCANCODE_DOWN;
const int K_Z      = SDL_SCANCODE_Z;
const int K_X      = SDL_SCANCODE_X;
const int K_C      = SDL_SCANCODE_C;
const int K_SHIFT  = SDL_SCANCODE_LSHIFT;
const int K_SPACE  = SDL_SCANCODE_SPACE;
const int K_RETURN = SDL_SCANCODE_RETURN;
const int K_ESC    = SDL_SCANCODE_ESCAPE;

/* RVSTACK: text entry rebuilt for pad-only input. Upstream captured
 * SDL_TEXTINPUT events from a real keyboard; the console has none, so the
 * high-score name is three arcade initials edited with the d-pad
 * (left/right = pick slot, up/down = cycle the character) via the
 * input_text_move/input_text_cycle calls in tetris.c's game-over screen. */
#define TEXT_LEN 3
static char textBuf[TEXT_LEN + 1] = "";
static int textCur = 0;
static int textActive = 0;
static const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.- ";

void input_start_text() {
	textBuf[0] = textBuf[1] = textBuf[2] = 'A';
	textBuf[TEXT_LEN] = '\0';
	textCur = 0;
	textActive = 1;
}

void input_stop_text() {
	textActive = 0;
}

const char *input_text() { return textBuf; }
int input_text_len() { return TEXT_LEN; }
int input_text_cursor() { return textActive ? textCur : -1; }

void input_text_move(int d) {
	if(!textActive) return;
	textCur = (textCur + d + TEXT_LEN) % TEXT_LEN;
}

void input_text_cycle(int d) {
	if(!textActive) return;
	int n = (int)sizeof(charset) - 1;
	int idx = 0;
	for(int i = 0; i < n; i++)
		if(charset[i] == textBuf[textCur]) { idx = i; break; }
	idx = (idx + d + n) % n;
	textBuf[textCur] = charset[idx];
}

int input_update() {
	SDL_Event event;
	while(SDL_PollEvent(&event)) {
		if(event.type == SDL_QUIT) return 1;
		/* RVSTACK: SDL_TEXTINPUT/SDLK_BACKSPACE handling removed — the pad
		 * selector above replaces it */
	}
	oldKey.left = key.left;
	oldKey.right = key.right;
	oldKey.up = key.up;
	oldKey.down = key.down;
	oldKey.z = key.z;
	oldKey.x = key.x;
	oldKey.c = key.c;
	oldKey.shift = key.shift;
	oldKey.space = key.space;
	oldKey.enter = key.enter;
	oldKey.esc = key.esc;
	const Uint8 *state = SDL_GetKeyboardState(NULL);
	key.up = state[K_UP];
	key.down = state[K_DOWN];
	key.left = state[K_LEFT];
	key.right = state[K_RIGHT];
	key.z = state[K_Z];
	key.x = state[K_X];
	key.c = state[K_C];
	key.shift = state[K_SHIFT];
	key.space = state[K_SPACE];
	key.enter = state[K_RETURN];
	key.esc = state[K_ESC];
	return 0;
}
