#include "sound.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>

#include "logsys.h"

/* RVSTACK: upstream Mix_LoadWAV'd the data/sfx WAVs from disk. The console has
 * no filesystem and sdl2_lite's mixer takes mono s16 @ 48 kHz buffers by
 * contract, so the WAVs are baked into the binary at build time by
 * tools/wav2c.py (compat/sfx_data.{c,h} — ~227 KB, which keeps the game
 * pak-free) and wrapped with Mix_QuickLoad_RAW. sfx_pcm[] order matches
 * the SFX_* enum in sound.h, like the file table it replaces. */
#include "sfx_data.h"

static Mix_Chunk *chunks[SFX_COUNT];
static int audio_ready = 0;

void sound_init() {
	if(SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
		log_msgf(ERROR, "SDL_InitSubSystem(AUDIO): %s\n", SDL_GetError());
		return;
	}
	/* RVSTACK: 48000 (the console stream's only rate), not 44100 */
	if(Mix_OpenAudio(48000, MIX_DEFAULT_FORMAT, 2, 512) < 0) {
		log_msgf(ERROR, "Mix_OpenAudio: %s\n", Mix_GetError());
		return;
	}
	// Plenty of channels so overlapping effects never cut each other off
	Mix_AllocateChannels(16);
	for(int i = 0; i < SFX_COUNT; i++) {
		/* RVSTACK: embedded buffers instead of Mix_LoadWAV(file) */
		chunks[i] = Mix_QuickLoad_RAW((Uint8 *)(uintptr_t)sfx_pcm[i],
		                              sfx_bytes[i]);
		if(!chunks[i]) {
			log_msgf(ERROR, "Mix_QuickLoad_RAW failed\n");
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
