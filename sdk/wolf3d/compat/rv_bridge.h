/*
 * rv_bridge.h — plain-typed seam between the Wolf4SDL compat layer and
 * sdl_lite. Only compat/lite_bridge.c includes sdl_lite.h (its SDL-1.2
 * types clash with compat/SDL.h's SDL2 shapes); everything else calls
 * through these plain-C signatures. Same architecture as sdk/tyrian.
 *
 * Part of the Wolf4SDL riscv-stack port glue (see compat/SDL.h).
 */
#ifndef RVSTACK_WOLF_RV_BRIDGE_H
#define RVSTACK_WOLF_RV_BRIDGE_H

#include <stdint.h>

void rvb_video_init(void);
void rvb_present_indexed(const void *pixels, int pitch, int w, int h,
                         const void *colors256);

/* 1 = keydown, 2 = keyup, 0 = none; *scancode = SDL2 scancode value */
int  rvb_poll_key(int *scancode);

/* callback audio over sdl_lite (pumped from Flip/Delay — no threads) */
int  rvb_audio_open(int channels, int samples,
                    void (*cb)(void *ud, uint8_t *stream, int len), void *ud);
void rvb_audio_pause(int pause_on);
void rvb_audio_close(void);

#endif /* RVSTACK_WOLF_RV_BRIDGE_H */
