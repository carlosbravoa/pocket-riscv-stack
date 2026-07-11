/*
 * music_stub.c — silence in place of the OPL music synth (riscv-stack port).
 *
 * Built when TYRIAN_MUSIC=0 (the default): replaces src/opl.c (DOSBox OPL2
 * emulator, float-heavy) and src/lds_play.c (Loudness player driving it).
 * The CPU is rv32im at 50 MHz with soft-float — the OPL emulator costs far
 * more than a frame per audio block, so music is stubbed to silence while
 * PCM sound effects (cheap integer mixing in loudness.c) stay fully alive.
 * Build with `make TYRIAN_MUSIC=1` to link the real synth instead (e.g. to
 * benchmark it on hardware).
 *
 * GPL-2.0-or-later (port glue; see compat/SDL.h).
 */
#include "lds_play.h"
#include "opl.h"

#include <string.h>

/* lds_play.c globals other files look at */
bool playing = false, songlooped = false;

bool lds_load(FILE *f, unsigned int music_offset, unsigned int music_size)
{
	(void)f; (void)music_offset; (void)music_size;
	playing = false;
	return true;
}

int  lds_update(void) { songlooped = true; return 0; }
void lds_free(void) {}
void lds_rewind(void) {}
void lds_fade(Uint8 speed) { (void)speed; }

/* opl.h entry points (loudness.c calls these through opl_* macros) */
void adlib_init(Bit32u samplerate) { (void)samplerate; }
void adlib_write(Bitu idx, Bit8u val) { (void)idx; (void)val; }

void adlib_getsample(Bit16s *sndptr, Bits numsamples)
{
	memset(sndptr, 0, (size_t)numsamples * sizeof(Bit16s));
}

Bitu adlib_reg_read(Bitu port) { (void)port; return 0; }
void adlib_write_index(Bitu port, Bit8u val) { (void)port; (void)val; }
