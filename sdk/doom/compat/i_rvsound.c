/*
 * i_rvsound.c — Doom SFX over the riscv-stack HAL's 48 kHz audio stream.
 *
 * This is DG_sound_module: an 8-channel
 * software mixer matching s_sound.c's snd_channels. There are NO threads —
 * the mix runs inside rvsound_pump(), which DG_DrawFrame calls once per
 * display frame: it synthesizes one frame of sound (48000/60 = 800 stereo
 * frames) and pushes it with audio_stream_write(). The write blocks on the
 * hardware FIFO, so audio doubles as pacing (the SDK's pump model; see
 * sdk/GUIDE.md).
 *
 * Sound lumps are vanilla DMX: 8-byte header (0x0003, rate u16, length
 * u32) + 16 pad bytes + unsigned 8-bit samples + 16 pad bytes. Each lump
 * is converted to signed 16-bit once, on first StartSound, and cached on
 * sfxinfo->driver_data (~2 MB total for shareware — the console heap is
 * 27 MB). Playback resamples nearest-neighbor with a 16.16 phase step;
 * vol/sep map to left/right gains the way i_sdlsound.c does.
 *
 * Music: DG_music_module lives in i_rvmusic.c (MUS sequencer -> OPL3 via
 * the vendored oplgm performer — no PCM rendered here). Its 140 Hz clock
 * is this pump: every batch of frames pushed to the stream also advances
 * rvmusic_advance(), so SFX and music share one call site — no threads.
 *
 * GPL-2.0-or-later (port glue; see ../ATTRIBUTION.md).
 */
#include "hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doomtype.h"
#include "i_sound.h"
#include "deh_str.h"
#include "m_misc.h"
#include "w_wad.h"
#include "z_zone.h"

#include "rvsound.h"

/* i_sound.c binds these config vars under FEATURE_SOUND; they belong to
 * the (unbuilt) SDL_mixer backend. Accept-and-ignore. */
int   use_libsamplerate  = 0;
float libsamplerate_scale = 1.0f;

#define NUM_CHANNELS   8            /* == s_sound.c's snd_channels */
#define OUT_RATE       48000
#define FRAME_SAMPLES  (OUT_RATE / 60)

typedef struct {
	int16_t *data;                  /* mono s16 at native rate */
	uint32_t nsamples;
	uint32_t rate;
} cached_sfx_t;

static struct {
	const cached_sfx_t *sfx;        /* NULL = idle */
	uint32_t pos, step;             /* 16.16 phase into sfx->data */
	int      lgain, rgain;          /* 0..255 */
} chan[NUM_CHANNELS];

static boolean use_sfx_prefix;
static boolean sound_initialized;

/* ------------------------------------------------------------ caching --- */

static void GetSfxLumpName(sfxinfo_t *sfx, char *buf, size_t buf_len)
{
	if (sfx->link != NULL)
		sfx = sfx->link;                /* linked lumps: use the target's */

	if (use_sfx_prefix)
		M_snprintf(buf, buf_len, "ds%s", DEH_String(sfx->name));
	else
		M_StringCopy(buf, DEH_String(sfx->name), buf_len);
}

/* Parse + convert one DMX lump; returns NULL if it isn't a valid sound. */
static cached_sfx_t *CacheSFX(sfxinfo_t *sfxinfo)
{
	int lumpnum = sfxinfo->lumpnum;
	byte *data = W_CacheLumpNum(lumpnum, PU_STATIC);
	unsigned int lumplen = W_LumpLength(lumpnum);

	/* DMX header: format 0x0003, u16 rate, u32 length (pads included) */
	if (lumplen < 8 || data[0] != 0x03 || data[1] != 0x00)
		return NULL;
	unsigned int rate = (data[3] << 8) | data[2];
	unsigned int length = (unsigned int)(data[7] << 24) | (data[6] << 16)
	                    | (data[5] << 8) | data[4];
	if (length > lumplen - 8 || length <= 48)
		return NULL;                    /* truncated / DMX-too-short */

	unsigned int nsamples = length - 32;    /* DMX skips 16 + 16 pad bytes */
	const byte *src = data + 8 + 16;

	cached_sfx_t *c = malloc(sizeof(*c));
	if (!c)
		return NULL;
	c->data = malloc(nsamples * sizeof(int16_t));
	if (!c->data) {
		free(c);
		return NULL;
	}
	for (unsigned int i = 0; i < nsamples; i++)
		c->data[i] = (int16_t)((src[i] - 128) << 8);
	c->nsamples = nsamples;
	c->rate     = rate;

	W_ReleaseLumpNum(lumpnum);          /* the s16 copy is ours now */
	return c;
}

/* ------------------------------------------------------------ the pump --- */

void rvsound_pump(void)
{
	static int16_t mix[FRAME_SAMPLES * 2];

	if (!sound_initialized)
		return;

	/* RVSTACK fix (field v0.19.10): a fixed 800-frame push per video frame
	 * assumes 60 fps — Doom ticks at 35, so the FIFO got ~28 k samples/s
	 * against a 48 kHz drain: chronic underrun, sound arriving in starved
	 * bursts seconds apart. Top up exactly what the FIFO can absorb and
	 * never block — the same discipline as sdl_lite's pump (v0.19.4). */
	for (;;) {
		int want = audio_stream_free();
		if (want > FRAME_SAMPLES)
			want = FRAME_SAMPLES;
		want &= ~1;
		if (want < 16)
			return;
		rvmusic_advance(want);          /* MUS ticks ride the sample clock */
		for (int i = 0; i < want; i++) {
			int32_t l = 0, r = 0;
			for (int ch = 0; ch < NUM_CHANNELS; ch++) {
				if (!chan[ch].sfx)
					continue;
				uint32_t idx = chan[ch].pos >> 16;
				if (idx >= chan[ch].sfx->nsamples) {
					chan[ch].sfx = NULL;
					continue;
				}
				int32_t s = chan[ch].sfx->data[idx];
				l += (s * chan[ch].lgain) >> 8;
				r += (s * chan[ch].rgain) >> 8;
				chan[ch].pos += chan[ch].step;
			}
			if (l > 32767) l = 32767;
			if (l < -32768) l = -32768;
			if (r > 32767) r = 32767;
			if (r < -32768) r = -32768;
			mix[2 * i]     = (int16_t)l;
			mix[2 * i + 1] = (int16_t)r;
		}
		audio_stream_write(mix, want);
	}
}

/* --------------------------------------------------------- the module --- */

static boolean I_RV_InitSound(boolean _use_sfx_prefix)
{
	use_sfx_prefix = _use_sfx_prefix;
	if (audio_stream_open(OUT_RATE) != 0) {
		printf("i_rvsound: no 48 kHz stream, sfx disabled\n");
		return false;
	}
	sound_initialized = true;
	printf("i_rvsound: 8-voice mixer on the %d Hz stream\n", OUT_RATE);
	return true;
}

static void I_RV_ShutdownSound(void)
{
	sound_initialized = false;
}

static int I_RV_GetSfxLumpNum(sfxinfo_t *sfx)
{
	char namebuf[9];
	GetSfxLumpName(sfx, namebuf, sizeof(namebuf));
	return W_GetNumForName(namebuf);
}

static void I_RV_UpdateSound(void)
{
	/* mixing happens in rvsound_pump() (from DG_DrawFrame) */
}

static void SetGains(int ch, int vol, int sep)
{
	int left  = ((254 - sep) * vol) / 127;      /* i_sdlsound's mapping */
	int right = (sep * vol) / 127;
	if (left < 0) left = 0;
	if (left > 255) left = 255;
	if (right < 0) right = 0;
	if (right > 255) right = 255;
	chan[ch].lgain = left;
	chan[ch].rgain = right;
}

static void I_RV_UpdateSoundParams(int ch, int vol, int sep)
{
	if (!sound_initialized || ch < 0 || ch >= NUM_CHANNELS)
		return;
	SetGains(ch, vol, sep);
}

static int I_RV_StartSound(sfxinfo_t *sfxinfo, int ch, int vol, int sep)
{
	if (!sound_initialized || ch < 0 || ch >= NUM_CHANNELS)
		return -1;

	if (sfxinfo->driver_data == NULL) {
		cached_sfx_t *c = CacheSFX(sfxinfo);
		if (c == NULL)
			return -1;
		sfxinfo->driver_data = c;
	}
	const cached_sfx_t *c = sfxinfo->driver_data;

	chan[ch].sfx  = c;
	chan[ch].pos  = 0;
	chan[ch].step = (uint32_t)(((uint64_t)c->rate << 16) / OUT_RATE);
	SetGains(ch, vol, sep);
	return ch;
}

static void I_RV_StopSound(int ch)
{
	if (ch < 0 || ch >= NUM_CHANNELS)
		return;
	chan[ch].sfx = NULL;
}

static boolean I_RV_SoundIsPlaying(int ch)
{
	if (ch < 0 || ch >= NUM_CHANNELS)
		return false;
	return chan[ch].sfx != NULL;
}

static void I_RV_PrecacheSounds(sfxinfo_t *sounds, int num_sounds)
{
	(void)sounds;
	(void)num_sounds;                   /* lazy-cached on first StartSound */
}

static snddevice_t sound_rv_devices[] = {
	SNDDEVICE_SB, SNDDEVICE_PAS, SNDDEVICE_GUS, SNDDEVICE_WAVEBLASTER,
	SNDDEVICE_SOUNDCANVAS, SNDDEVICE_AWE32,
};

sound_module_t DG_sound_module = {
	sound_rv_devices,
	arrlen(sound_rv_devices),
	I_RV_InitSound,
	I_RV_ShutdownSound,
	I_RV_GetSfxLumpNum,
	I_RV_UpdateSound,
	I_RV_UpdateSoundParams,
	I_RV_StartSound,
	I_RV_StopSound,
	I_RV_SoundIsPlaying,
	I_RV_PrecacheSounds,
};
