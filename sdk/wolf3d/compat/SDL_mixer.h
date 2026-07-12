/* SDL_mixer.h — compat shim: the game only wants MIX_CHANNELS (the digi
 * channel count, mixed by compat/id_sd_rv.c). No SDL_mixer exists here.
 * Part of the Wolf4SDL riscv-stack port glue (see compat/SDL.h). */
#ifndef RVSTACK_WOLF_SDL_MIXER_H
#define RVSTACK_WOLF_SDL_MIXER_H

#include "SDL.h"

#define MIX_CHANNELS 8          /* keep in sync with compat/id_sd_rv.c */

#endif
