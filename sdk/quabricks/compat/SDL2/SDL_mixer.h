/*
 * SDL2/SDL_mixer.h — shadow header for the quabricks riscv-stack port.
 * The Mix_* subset lives in sdl2_lite: an 8-channel one-shot software
 * mixer over the HAL stream, pumped non-blocking from RenderPresent/Delay
 * (see the MIXER doc block in sdk/sdl2_lite.h). Chunks are mono s16 @
 * 48 kHz by contract — assets are baked by tools/wav2c.py, so there is no
 * Mix_LoadWAV here (sound.c uses Mix_QuickLoad_RAW instead, RVSTACK-marked).
 * MIT (port glue; inherits the game's license, see ../../LICENSE).
 */
#ifndef RVSTACK_QUABRICKS_SDL2_MIXER_SHADOW_H
#define RVSTACK_QUABRICKS_SDL2_MIXER_SHADOW_H

#include "sdl2_lite.h"

#endif
