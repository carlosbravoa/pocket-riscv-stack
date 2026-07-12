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

// Text-entry state
#define TEXT_MAX 15
static char textBuf[TEXT_MAX + 1] = "";
static int textLen = 0;
static int textActive = 0;

void input_start_text() {
	textBuf[0] = '\0';
	textLen = 0;
	textActive = 1;
	SDL_StartTextInput();
}

void input_stop_text() {
	textActive = 0;
	SDL_StopTextInput();
}

const char *input_text() { return textBuf; }
int input_text_len() { return textLen; }

int input_update() {
	SDL_Event event;
	while(SDL_PollEvent(&event)) {
		if(event.type == SDL_QUIT) return 1;
		if(textActive) {
			if(event.type == SDL_TEXTINPUT) {
				for(const char *p = event.text.text; *p && textLen < TEXT_MAX; p++) {
					// keep it to plain printable ASCII
					if((unsigned char)*p >= 0x20 && (unsigned char)*p < 0x7F)
						textBuf[textLen++] = *p;
				}
				textBuf[textLen] = '\0';
			} else if(event.type == SDL_KEYDOWN &&
					event.key.keysym.sym == SDLK_BACKSPACE && textLen > 0) {
				textBuf[--textLen] = '\0';
			}
		}
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
