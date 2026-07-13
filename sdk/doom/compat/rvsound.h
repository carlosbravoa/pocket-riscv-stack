/*
 * rvsound.h — the DOOM port's audio pump seam (SFX: compat/i_rvsound.c,
 * OPL3 music: compat/i_rvmusic.c).
 * GPL-2.0-or-later (port glue; see ../ATTRIBUTION.md).
 */
#ifndef RVSTACK_RVSOUND_H
#define RVSTACK_RVSOUND_H

/* Top up the HAL's 48 kHz stream with mixed SFX (only what
 * audio_stream_free() reports — never blocking). DG_DrawFrame calls this
 * once per frame. No-op until the sound module initializes. */
void rvsound_pump(void);

/* Advance the MUS sequencer by nframes stream samples (i_rvmusic.c);
 * rvsound_pump calls it for every batch it pushes, so the sample counter
 * is the 140 Hz music clock. No-op when nothing is playing. */
void rvmusic_advance(int nframes);

#endif
