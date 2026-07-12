#include "sound.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>

#include "logsys.h"

// File names line up with the SFX_* enum in sound.h
static const char *sfx_files[SFX_COUNT] = {
	"data/sfx/move.wav",
	"data/sfx/rotate.wav",
	"data/sfx/softdrop.wav",
	"data/sfx/harddrop.wav",
	"data/sfx/lock.wav",
	"data/sfx/lineclear.wav",
	"data/sfx/tetris.wav",
	"data/sfx/levelup.wav",
	"data/sfx/gameover.wav",
	"data/sfx/menu_move.wav",
	"data/sfx/menu_select.wav",
	"data/sfx/hold.wav",
	"data/sfx/pause.wav",
};

static Mix_Chunk *chunks[SFX_COUNT];
static int audio_ready = 0;

void sound_init() {
	if(SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
		log_msgf(ERROR, "SDL_InitSubSystem(AUDIO): %s\n", SDL_GetError());
		return;
	}
	// 44100Hz, 16-bit, mono content played on a stereo bus, small buffer for
	// low latency so drops/locks feel responsive
	if(Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 512) < 0) {
		log_msgf(ERROR, "Mix_OpenAudio: %s\n", Mix_GetError());
		return;
	}
	// Plenty of channels so overlapping effects never cut each other off
	Mix_AllocateChannels(16);
	for(int i = 0; i < SFX_COUNT; i++) {
		chunks[i] = Mix_LoadWAV(sfx_files[i]);
		if(!chunks[i]) {
			log_msgf(ERROR, "Mix_LoadWAV(%s): %s\n", sfx_files[i], Mix_GetError());
		}
	}
	audio_ready = 1;
	log_msgf(INFO, "Audio initialized.\n");
}

void sound_play(int id) {
	if(!audio_ready) return;
	if(id < 0 || id >= SFX_COUNT || !chunks[id]) return;
	Mix_PlayChannel(-1, chunks[id], 0);
}

void sound_quit() {
	if(!audio_ready) return;
	for(int i = 0; i < SFX_COUNT; i++) {
		if(chunks[i]) Mix_FreeChunk(chunks[i]);
	}
	Mix_CloseAudio();
	audio_ready = 0;
}
