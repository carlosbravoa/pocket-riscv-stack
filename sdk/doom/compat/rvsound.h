/*
 * rvsound.h — the DOOM port's SFX pump (see compat/i_rvsound.c).
 * GPL-2.0-or-later (port glue; see ../ATTRIBUTION.md).
 */
#ifndef RVSTACK_RVSOUND_H
#define RVSTACK_RVSOUND_H

/* Mix one display frame of sound (800 stereo frames @48 kHz) and push it
 * to the HAL stream. DG_DrawFrame calls this once per frame; the blocking
 * write doubles as pacing. No-op until the sound module initializes. */
void rvsound_pump(void);

#endif
