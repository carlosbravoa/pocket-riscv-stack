#ifndef TETRIS_SOUND
#define TETRIS_SOUND

// Sound effect identifiers, order matches the loaded file table in sound.c
enum {
	SFX_MOVE,
	SFX_ROTATE,
	SFX_SOFTDROP,
	SFX_HARDDROP,
	SFX_LOCK,
	SFX_LINECLEAR,
	SFX_TETRIS,
	SFX_LEVELUP,
	SFX_GAMEOVER,
	SFX_MENU_MOVE,
	SFX_MENU_SELECT,
	SFX_HOLD,
	SFX_PAUSE,
	SFX_COUNT
};

// Open the audio device and load every sound effect. Safe to call once at
// startup; failures are logged but non-fatal (the game runs silently).
void sound_init();

// Play a sound effect by id (see enum above)
void sound_play(int id);

// Free chunks and close the audio device
void sound_quit();

#endif
