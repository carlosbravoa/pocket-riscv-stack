/*
 * rv_bridge.h — plain-typed seam between the SDLPoP compat layer and sdl_lite.
 * Only compat/lite_bridge.c includes sdl_lite.h (its SDL-1.2 types clash with
 * compat/SDL.h's SDL2 shapes); everything else calls through these plain-C
 * signatures. Same architecture as sdk/wolf3d and sdk/tyrian.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#ifndef POP_COMPAT_RV_BRIDGE_H
#define POP_COMPAT_RV_BRIDGE_H

#include <stdint.h>

/* Bring up the 320x200 indexed video mode + dev HUD. */
void rvb_video_init(void);

/* Present a full 8bpp-indexed frame (colors256 = 256x3 RGB888, or NULL). */
void rvb_present_indexed(const void *pixels, int pitch, int w, int h,
                         const void *colors256);

/* Load-progress beacon (0xBEAC000n on diag + a colored bar); survives fades. */
void rvb_progress(int stage);

/* Callback audio over sdl_lite (pumped from the present + waits; no threads). */
int  rvb_audio_open(int channels, int samples,
                    void (*cb)(void *ud, uint8_t *stream, int len), void *ud);
void rvb_audio_pause(int pause_on);
void rvb_audio_close(void);
void rvb_audio_pump(void);          /* feed the FIFO from dead-wait time */

#endif /* POP_COMPAT_RV_BRIDGE_H */
