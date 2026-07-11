/*
 * rv_bridge.h — private interface between the SDL2-flavored shim
 * (compat/sdl2_shim.c, which includes compat/SDL.h) and the sdl_lite side
 * (compat/lite_bridge.c, which includes sdk/sdl_lite.h). The two headers
 * define clashing SDL types, so this bridge speaks plain C types only.
 *
 * GPL-2.0-or-later (port glue; see compat/SDL.h).
 */
#ifndef RVSTACK_RV_BRIDGE_H
#define RVSTACK_RV_BRIDGE_H

#include <stdint.h>

/* video: 320x200x8 mode + present (pixels+palette -> SDL_Flip) */
void rvb_video_init(void);
void rvb_present_indexed(const void *pixels, int pitch, int w, int h,
                         const void *colors256 /* 256 x {r,g,b,x} bytes */);

/* events: poll one key edge. Returns 0 = none, 1 = down, 2 = up.
 * *scancode receives an SDL2 scancode value (translated from the pad). */
int rvb_poll_key(int *scancode);

/* audio: open the lite callback stream. Returns the real rate (48000) or
 * -1. Callback contract matches SDL: cb(userdata, stream, len_bytes). */
int  rvb_audio_open(int channels, int samples,
                    void (*cb)(void *ud, uint8_t *stream, int len), void *ud);
void rvb_audio_pause(int pause_on);
void rvb_audio_close(void);

#endif /* RVSTACK_RV_BRIDGE_H */
