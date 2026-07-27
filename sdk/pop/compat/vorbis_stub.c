/*
 * vorbis_stub.c — the SDLPoP port drops stb_vorbis.c (float-heavy Ogg decode,
 * ACCEL.md red flag). Ogg replacement music is unsupported; the game's OGG
 * paths (seg009.c ogg_callback) get these silent stubs. The prototypes come
 * from stb_vorbis.c in header-only mode (included via types.h).
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"          /* -Isrc: declarations + stb_vorbis typedef */

stb_vorbis *stb_vorbis_open_memory(const unsigned char *data, int len,
                                   int *error, const stb_vorbis_alloc *alloc)
{ (void)data; (void)len; (void)alloc; if (error) *error = 1; return 0; }

int stb_vorbis_get_samples_short_interleaved(stb_vorbis *f, int channels,
                                             short *buffer, int num_shorts)
{ (void)f; (void)channels; (void)buffer; (void)num_shorts; return 0; }

int stb_vorbis_seek_start(stb_vorbis *f) { (void)f; return 0; }

unsigned int stb_vorbis_stream_length_in_samples(stb_vorbis *f) { (void)f; return 0; }

void stb_vorbis_close(stb_vorbis *f) { (void)f; }
