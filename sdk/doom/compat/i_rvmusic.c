/*
 * i_rvmusic.c — Doom music on the FM flavor's genuine OPL3.
 *
 * DG_music_module: a MUS sequencer feeding oplgm.c (sdk/midiplay's
 * 18-voice General MIDI performer, vendored) with patches from the WAD's
 * own GENMIDI lump — which IS DMX .op2 format ("#OPL_II#" magic; bank.c
 * checks and skips it). No sample rendering happens here: oplgm emits an
 * opl_write() register stream and the hardware chip does the synthesis.
 *
 * Clocking: NO threads, NO floats. rvsound_pump() (compat/i_rvsound.c)
 * calls rvmusic_advance(n) for every n stereo frames it pushes to the
 * 48 kHz stream, and a fractional accumulator turns that sample count
 * into 140 Hz MUS ticks (48000/140 = 342 6/7 samples per tick, exact in
 * integers as acc += n*140; tick while acc >= 48000). Music and SFX thus
 * share one pump and stay in lockstep with real time.
 *
 * Everything gates on sys_caps()->features & HAL_FEAT_FM: on PCM-only
 * flavors every entry point is a silent no-op and opl_write is never
 * called.
 *
 * MUS format (DMX): "MUS\x1A", u16 scorelen, u16 scorestart, then events.
 * Event byte = last<<7 | type<<4 | channel. Types: 0 release (note),
 * 1 play (bit7 of note = velocity byte follows), 2 pitch (byte, 128 =
 * center, full scale = +/-2 semitones), 3 system event (10 all-sounds-
 * off, 11 all-notes-off, 14 reset-controllers), 4 controller (number,
 * value; number 0 = instrument change, 3 = volume, 4 = pan, 5 =
 * expression, 8 = sustain), 6 score end (loop point). When 'last' is
 * set a VLQ tick delay follows. MUS channel 15 is percussion -> MIDI
 * channel 10 (oplgm's dedicated percussion path).
 *
 * GPL-2.0-or-later (port glue; see ../ATTRIBUTION.md).
 */
#include "hal.h"

#include <stdio.h>
#include <stdlib.h>

#include "doomtype.h"
#include "i_sound.h"
#include "deh_str.h"
#include "w_wad.h"
#include "z_zone.h"

#include "bank.h"
#include "oplgm.h"
#include "rvsound.h"

#define MUS_RATE   140                  /* DMX tick rate, Hz */
#define OUT_RATE   48000                /* == i_rvsound.c's stream rate */

/* MUS channel -> MIDI channel: 15 is percussion (MIDI ch10 == index 9);
 * 9..14 shift up to keep clear of it. Doom's own lumps only use 0-8 + 15. */
static const uint8_t mus2midi[16] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 15, 9
};

typedef struct {
	const byte *score;                  /* event stream (inside the lump) */
	uint32_t    len;                    /* score length in bytes */
} mus_song_t;

static boolean fm_present;              /* HAL_FEAT_FM at init */
static bank_t  genmidi_bank;
static int     bank_state;              /* 0 untried, 1 loaded, -1 failed */

static mus_song_t *song;                /* registered + playing, or NULL */
static boolean  playing, paused, looping;
static uint32_t pos;                    /* read cursor into song->score */
static uint32_t wait_ticks;             /* MUS ticks until the next event */
static uint32_t acc;                    /* sample->tick fraction, x MUS_RATE */

static int     music_vol = 127;         /* I_SetMusicVolume, 0..127 */
static uint8_t chan_vol[16];            /* per-MIDI-channel MUS CC volume */
static uint8_t last_vel[16];            /* MUS play-note velocity memory */

/* ------------------------------------------------------------- patches --- */

static int LoadGENMIDI(void)
{
	if (bank_state)
		return bank_state;

	int lumpnum = W_CheckNumForName(DEH_String("GENMIDI"));
	if (lumpnum >= 0) {
		byte *data = W_CacheLumpNum(lumpnum, PU_STATIC);
		unsigned int len = W_LumpLength(lumpnum);
		/* the lump is a complete .op2 file: bank_load verifies the
		 * 8-byte "#OPL_II#" header and skips it */
		if (bank_load(&genmidi_bank, "GENMIDI", data, len) == 0)
			bank_state = 1;
		W_ReleaseLumpNum(lumpnum);      /* bank_t holds its own copy */
	}
	if (bank_state != 1) {
		printf("i_rvmusic: no usable GENMIDI lump, music off\n");
		bank_state = -1;
		return bank_state;
	}
	oplgm_set_bank(&genmidi_bank, NULL);
	printf("i_rvmusic: GENMIDI bank -> OPL3, MUS sequencer at %d Hz\n",
	       MUS_RATE);
	return bank_state;
}

/* -------------------------------------------------------------- volume --- */

/* MUS channel volume and the menu music slider both land on oplgm's CC7
 * (dB-domain attenuation next to velocity/expression). */
static void ApplyVolume(int midi_ch)
{
	oplgm_control(midi_ch, 7, (chan_vol[midi_ch] * music_vol) / 127);
}

/* --------------------------------------------------------- the sequencer --- */

static byte ReadByte(void)
{
	if (song == NULL || pos >= song->len) {
		playing = false;                /* truncated lump: stop, don't wrap */
		return 0;
	}
	return song->score[pos++];
}

static uint32_t ReadDelay(void)         /* VLQ, 7 bits per byte */
{
	uint32_t d = 0;
	for (int i = 0; i < 5 && playing; i++) {
		byte b = ReadByte();
		d = (d << 7) | (b & 0x7F);
		if (!(b & 0x80))
			break;
	}
	return d;
}

static void SongRewind(void)
{
	pos = 0;
	wait_ticks = 0;
	acc = 0;
	oplgm_all_off();
	for (int c = 0; c < 16; c++) {      /* fresh GM state for every run */
		oplgm_control(c, 121, 0);       /* reset controllers */
		oplgm_program(c, 0);
		chan_vol[c] = 100;              /* GM default until MUS CC3 lands */
		last_vel[c] = 100;
		ApplyVolume(c);
	}
}

/* Run event groups until a nonzero delay (or the song ends). */
static void RunEvents(void)
{
	int wrapped = 0;

	while (playing && wait_ticks == 0) {
		byte b   = ReadByte();
		int last = b & 0x80;
		int type = (b >> 4) & 7;
		int ch   = mus2midi[b & 0x0F];

		switch (type) {
		case 0: {                       /* release note */
			oplgm_note_off(ch, ReadByte() & 0x7F);
			break;
		}
		case 1: {                       /* play note */
			byte n = ReadByte();
			if (n & 0x80)
				last_vel[ch] = ReadByte() & 0x7F;
			oplgm_note_on(ch, n & 0x7F, last_vel[ch]);
			break;
		}
		case 2:                         /* pitch wheel: 0..255, 128 center */
			oplgm_bend(ch, (int)ReadByte() << 6);   /* -> 0..16320, 8192 mid */
			break;
		case 3: {                       /* system event */
			switch (ReadByte() & 0x7F) {
			case 10: oplgm_control(ch, 120, 0); break;  /* all sounds off */
			case 11: oplgm_control(ch, 123, 0); break;  /* all notes off */
			case 14: oplgm_control(ch, 121, 0); break;  /* reset ctrls */
			default: break;             /* 12/13 mono/poly: meaningless here */
			}
			break;
		}
		case 4: {                       /* change controller */
			byte c = ReadByte() & 0x7F;
			byte v = ReadByte() & 0x7F;
			switch (c) {
			case 0: oplgm_program(ch, v); break;        /* instrument! */
			case 3:                                     /* channel volume */
				chan_vol[ch] = v;
				ApplyVolume(ch);
				break;
			case 4: oplgm_control(ch, 10, v); break;    /* pan */
			case 5: oplgm_control(ch, 11, v); break;    /* expression */
			case 8: oplgm_control(ch, 64, v); break;    /* sustain */
			default: break;             /* bank/mod/reverb/chorus: no-op */
			}
			break;
		}
		case 6:                         /* score end */
			if (looping && !wrapped && pos > 1) {
				wrapped = 1;            /* one wrap per call: an empty or
				                         * degenerate score can't spin */
				SongRewind();
				continue;
			}
			oplgm_all_off();
			playing = false;
			return;
		default:                        /* types 5/7 are unused in DMX;
			                             * seeing one means garbage */
			printf("i_rvmusic: bad MUS event %d, stopping\n", type);
			oplgm_all_off();
			playing = false;
			return;
		}

		if (last)
			wait_ticks = ReadDelay();
	}
}

/* Called from rvsound_pump() for every nframes pushed to the 48 kHz
 * stream — the sample counter IS the sequencer clock. */
void rvmusic_advance(int nframes)
{
	if (!playing || paused)
		return;
	acc += (uint32_t)nframes * MUS_RATE;
	while (acc >= OUT_RATE && playing) {
		acc -= OUT_RATE;
		if (wait_ticks > 0)
			wait_ticks--;
		if (wait_ticks == 0)
			RunEvents();
	}
}

/* ------------------------------------------------------------ the module --- */

static boolean I_RV_InitMusic(void)
{
	fm_present = (sys_caps()->features & HAL_FEAT_FM) != 0;
	if (!fm_present) {
		printf("i_rvmusic: no OPL3 on this flavor, music disabled\n");
		return true;                    /* quiet no-op module, not an error */
	}
	oplgm_init();                       /* NEW mode on, 18x2-op, all silent */
	LoadGENMIDI();                      /* WADs are in by I_InitMusic time */
	return true;
}

static void I_RV_ShutdownMusic(void)
{
	if (!fm_present)
		return;
	playing = false;
	oplgm_all_off();
}

static void I_RV_SetMusicVolume(int volume)
{
	if (volume < 0)   volume = 0;
	if (volume > 127) volume = 127;
	music_vol = volume;
	if (!fm_present)
		return;
	for (int c = 0; c < 16; c++)
		ApplyVolume(c);
}

static void I_RV_PauseSong(void)
{
	if (!fm_present || !playing)
		return;
	paused = true;
	oplgm_all_off();                    /* held notes would drone forever */
}

static void I_RV_ResumeSong(void)
{
	paused = false;
}

static void *I_RV_RegisterSong(void *data, int len)
{
	if (!fm_present || data == NULL || len < 16)
		return NULL;
	const byte *d = data;
	if (d[0] != 'M' || d[1] != 'U' || d[2] != 'S' || d[3] != 0x1A)
		return NULL;                    /* not MUS (PWAD MIDI etc.): skip */

	uint32_t scorelen   = d[4] | ((uint32_t)d[5] << 8);
	uint32_t scorestart = d[6] | ((uint32_t)d[7] << 8);
	if (scorestart >= (uint32_t)len)
		return NULL;
	if (scorelen == 0 || scorestart + scorelen > (uint32_t)len)
		scorelen = (uint32_t)len - scorestart;      /* trust the lump size */

	mus_song_t *s = malloc(sizeof(*s));
	if (s == NULL)
		return NULL;
	s->score = d + scorestart;          /* zone data stays cached while
	                                     * registered (s_sound owns it) */
	s->len   = scorelen;
	return s;
}

static void I_RV_UnRegisterSong(void *handle)
{
	if (handle == NULL)
		return;
	if (song == handle) {
		playing = false;
		if (fm_present)
			oplgm_all_off();
		song = NULL;
	}
	free(handle);
}

static void I_RV_PlaySong(void *handle, boolean do_loop)
{
	if (!fm_present || handle == NULL || LoadGENMIDI() != 1)
		return;
	song    = handle;
	looping = do_loop;
	paused  = false;
	SongRewind();
	playing = true;                     /* first events fire on tick 0 */
}

static void I_RV_StopSong(void)
{
	playing = false;
	song = NULL;
	if (fm_present)
		oplgm_all_off();
}

static boolean I_RV_MusicIsPlaying(void)
{
	return playing;
}

static void I_RV_PollMusic(void)
{
	/* sequencing happens in rvmusic_advance() (from rvsound_pump) */
}

static snddevice_t music_rv_devices[] = {
	SNDDEVICE_ADLIB, SNDDEVICE_SB, SNDDEVICE_GENMIDI,
};

music_module_t DG_music_module = {
	music_rv_devices,
	arrlen(music_rv_devices),
	I_RV_InitMusic,
	I_RV_ShutdownMusic,
	I_RV_SetMusicVolume,
	I_RV_PauseSong,
	I_RV_ResumeSong,
	I_RV_RegisterSong,
	I_RV_UnRegisterSong,
	I_RV_PlaySong,
	I_RV_StopSong,
	I_RV_MusicIsPlaying,
	I_RV_PollMusic,
};
