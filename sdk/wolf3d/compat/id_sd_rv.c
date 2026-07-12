/*
 * id_sd_rv.c — Wolfenstein 3D's Sound Manager on the riscv-stack HAL.
 * Replaces the vendored id_sd.c (SDL_mixer + a software OPL emulator).
 *
 * The showpiece: Wolf3D's music is IMF — literal timestamped OPL register
 * dumps — and its AdLib SFX are OPL register programs too. On the FM
 * flavor this file forwards every register byte to the REAL OPL3 via
 * opl_write(); the chip does the synthesis and core_top mixes FM into the
 * DAC after our PCM. Zero DSP on the CPU; rv32im never touches a float
 * for music. Same seam as sdk/tyrian/compat/opl3_hw.c.
 *
 * Timing (no threads, no interrupts): the SDL_lite audio callback — pumped
 * from SDL_Flip()/SDL_Delay() — is the clock. Exactly like the original
 * SDL_IMFMusicPlayer, the callback advances the 700 Hz sequencer by
 * counting the 48 kHz samples it emits: register writes land sample-
 * accurately relative to the PCM stream on both the console and the PC
 * twin (where opl_write logs to $RVSTACK_OPLLOG instead of a chip).
 *
 * Digitized SFX (the VSWAP 7042 Hz 8-bit pages) are resampled in the same
 * callback with a 16.16 stepper — no float, no startup conversion pass,
 * zero-copy from the PM page cache. 8 channels, 2 reserved (player/boss
 * weapons), stereo panning from SD_SetPosition, exactly the SDL_mixer
 * channel semantics wl_game.c expects.
 *
 * On flavors without FM (sys_caps), opl_write is a no-op upstream and
 * everything here still runs — music is silently absent, digitized SFX
 * carry the game. One binary, any flavor.
 *
 * OPL2-program-on-OPL3 notes (from the Tyrian port):
 *  - The chip runs in OPL3 NEW mode (0x105 bit0), the mode the flavor's
 *    synth is validated in. NEW mode adds L/R output-enable bits to the
 *    0xC0 channel registers that OPL2 programs leave 0 (= silence), so
 *    0xC0-0xC8 writes get OR'd with 0x30 (both speakers).
 *  - hal.h MUST be included before any header that could mangle its
 *    declarations; id_sd.h's alOut macro is #undef'd below.
 *
 * Part of the Wolf4SDL riscv-stack port glue (see compat/SDL.h).
 */
#include "hal.h"                /* opl_write, sys_caps — FIRST (trap #2) */

#include "wl_def.h"
#undef alOut                    /* id_sd.h maps it to the emulator; ours is real */

#include "rv_bridge.h"

#include <string.h>

#define ORIGSAMPLERATE  7042    /* VSWAP digitized sample rate */
#define OUTRATE         48000   /* the HAL's one stream rate */
#define MUSIC_HZ        700     /* SDL_t0FastAsmService rate (IMF ticks) */
#define SFX_DIV         5       /* AdLib SFX service = 700/5 = 140 Hz */
#define MIX_CHANNELS    8

/* ------------------------------------------------------------ globals -- */

globalsoundpos channelSoundPos[MIX_CHANNELS];

boolean AdLibPresent, SoundBlasterPresent, SBProPresent, SoundPositioned;
byte    SoundMode, MusicMode, DigiMode;
int     DigiMap[LASTSOUND];
int     DigiChannel[STARTMUSIC - STARTDIGISOUNDS];
word    NumDigi;
digiinfo *DigiList;

static byte   **SoundTable;
static boolean  SD_Started;
static boolean  nextsoundpos;
static int      SoundNumber, DigiNumber;
static word     SoundPriority, DigiPriority;
static int      LeftPosition, RightPosition;
static boolean  DigiPlaying;

/* ------------------------------------------------- the real OPL3 seam -- */

static uint8_t opl_shadow[256];         /* OPL2 register file mirror */
static int     have_fm = -1;

static int fm(void)
{
	if (have_fm < 0)
		have_fm = (sys_caps()->features & HAL_FEAT_FM) ? 1 : 0;
	return have_fm;
}

static void alOut(byte reg, byte val)
{
	opl_shadow[reg] = val;
	if (!fm())
		return;
	if ((reg & 0xF0) == 0xC0)
		val |= 0x30;                    /* L+R enable (NEW-mode semantics) */
	opl_write(reg, val);
}

static void opl_reset(void)
{
	memset(opl_shadow, 0, sizeof(opl_shadow));
	if (!fm())
		return;
	for (unsigned r = 0x20; r <= 0xF5; r++) {
		uint8_t v = 0;
		if ((r & 0xF0) == 0xC0)
			v = 0x30;
		opl_write((uint16_t)r, v);
	}
	opl_write(0x105, 0x01);             /* NEW=1: the validated mode */
	opl_write(0x104, 0x00);             /* no 4-op pairings */
	opl_write(0x08,  0x00);
	opl_write(0xBD,  0x00);             /* melodic mode */
}

/* --------------------------------------------------- AdLib SFX state --- */

static byte * volatile alSound;         /* 140 Hz freq-byte stream */
static byte            alBlock;
static longword        alLengthLeft;
static Instrument      alZeroInst;

static void SDL_ALStopSound(void)
{
	alSound = 0;
	alOut(alFreqH + 0, 0);
}

static void SDL_AlSetFXInst(Instrument *inst)
{
	byte c, m;

	m = 0;                              /* modulator cell for channel 0 */
	c = 3;                              /* carrier cell for channel 0 */
	alOut(m + alChar,   inst->mChar);
	alOut(m + alScale,  inst->mScale);
	alOut(m + alAttack, inst->mAttack);
	alOut(m + alSus,    inst->mSus);
	alOut(m + alWave,   inst->mWave);
	alOut(c + alChar,   inst->cChar);
	alOut(c + alScale,  inst->cScale);
	alOut(c + alAttack, inst->cAttack);
	alOut(c + alSus,    inst->cSus);
	alOut(c + alWave,   inst->cWave);
	alOut(alFeedCon, 0);
}

static void SDL_ALPlaySound(AdLibSound *sound)
{
	SDL_ALStopSound();

	alLengthLeft = sound->common.length;
	alBlock = ((sound->block & 7) << 2) | 0x20;

	if (!(sound->inst.mSus | sound->inst.cSus))
		Quit("SDL_ALPlaySound() - Bad instrument");

	SDL_AlSetFXInst(&sound->inst);
	alSound = (byte *)sound->data;
}

static void SDL_ShutAL(void)
{
	alSound = 0;
	alOut(alEffects, 0);
	alOut(alFreqH + 0, 0);
	SDL_AlSetFXInst(&alZeroInst);
}

static void SDL_StartAL(void)
{
	alOut(alEffects, 0);
	SDL_AlSetFXInst(&alZeroInst);
}

/* --------------------------------------------------- sequencer (IMF) --- */

static volatile boolean sqActive;
static word            *sqHack, *sqHackPtr;
static int              sqHackLen, sqHackSeqLen;
static longword         sqHackTime;
static longword         alTimeCount;

/* ------------------------------------------------- digitized channels -- */

typedef struct {
	const byte *data;                   /* 8-bit unsigned, 7042 Hz (VSWAP) */
	uint32_t    len;                    /* samples */
	uint32_t    pos;                    /* 16.16 */
	int         lvol, rvol;             /* 0..256 */
	int         active;
} digichan_t;

static digichan_t chans[MIX_CHANNELS];
static struct { const byte *data; uint32_t len; } digicache[STARTMUSIC - STARTDIGISOUNDS];

#define DIGI_STEP ((uint32_t)(((uint64_t)ORIGSAMPLERATE << 16) / OUTRATE))

/* --------------------------------------------------- the mixer clock --- */

static int  readysamples;               /* samples until the next 700Hz tick */
static int  tickphase;                  /* fractional accumulator (per-tick) */
static int  soundTimeCounter = SFX_DIV; /* 140 Hz divider for AdLib SFX */

static byte * volatile curAlSound;
static byte           *curAlSoundPtr;
static longword        curAlLengthLeft;

static void SDL_SoundFinished(void)
{
	SoundNumber = 0;
	SoundPriority = 0;
}

/* one 700 Hz tick: AdLib SFX service (at /5) + IMF sequencer service */
static void music_tick(void)
{
	if (--soundTimeCounter == 0) {
		soundTimeCounter = SFX_DIV;
		if (curAlSound != alSound) {
			curAlSound = curAlSoundPtr = alSound;
			curAlLengthLeft = alLengthLeft;
		}
		if (curAlSound) {
			if (*curAlSoundPtr) {
				alOut(alFreqL, *curAlSoundPtr);
				alOut(alFreqH, alBlock);
			} else
				alOut(alFreqH, 0);
			curAlSoundPtr++;
			curAlLengthLeft--;
			if (!curAlLengthLeft) {
				curAlSound = alSound = 0;
				SoundNumber = 0;
				SoundPriority = 0;
				alOut(alFreqH, 0);
			}
		}
	}
	if (sqActive) {
		do {
			if (sqHackTime > alTimeCount)
				break;
			sqHackTime = alTimeCount + *(sqHackPtr + 1);
			alOut(*(byte *)sqHackPtr, *(((byte *)sqHackPtr) + 1));
			sqHackPtr += 2;
			sqHackLen -= 4;
		} while (sqHackLen > 0);
		alTimeCount++;
		if (!sqHackLen) {
			sqHackPtr = sqHack;
			sqHackLen = sqHackSeqLen;
			sqHackTime = 0;
			alTimeCount = 0;
		}
	}
}

static void mix_digi(int16_t *out, int nframes)
{
	for (int i = 0; i < nframes; i++) {
		int32_t l = 0, r = 0;
		for (int c = 0; c < MIX_CHANNELS; c++) {
			digichan_t *ch = &chans[c];
			if (!ch->active)
				continue;
			uint32_t idx = ch->pos >> 16;
			if (idx >= ch->len) {
				ch->active = 0;
				channelSoundPos[c].valid = 0;
				continue;
			}
			int32_t s = ((int32_t)ch->data[idx] - 128) << 7;
			l += (s * ch->lvol) >> 8;
			r += (s * ch->rvol) >> 8;
			ch->pos += DIGI_STEP;
		}
		if (l > 32767) l = 32767;
		if (l < -32768) l = -32768;
		if (r > 32767) r = 32767;
		if (r < -32768) r = -32768;
		out[2 * i]     = (int16_t)l;
		out[2 * i + 1] = (int16_t)r;
	}
}

/* The audio callback — the port's only "interrupt". Chunked exactly like
 * the original SDL_IMFMusicPlayer: emit samples until the next 700 Hz
 * boundary, run one tick, repeat. */
static void SDW_AudioCallback(void *udata, uint8_t *stream, int len)
{
	(void)udata;
	int16_t *out = (int16_t *)(void *)stream;
	int frames = len >> 2;              /* 16-bit stereo */

	while (frames > 0) {
		if (readysamples <= 0) {
			music_tick();
			tickphase += OUTRATE;
			readysamples = tickphase / MUSIC_HZ;
			tickphase   %= MUSIC_HZ;
		}
		int n = (readysamples < frames) ? readysamples : frames;
		mix_digi(out, n);
		out += 2 * n;
		frames       -= n;
		readysamples -= n;
	}
}

/* ------------------------------------------------------------- timing -- */

void Delay(int32_t wolfticks)
{
	if (wolfticks > 0)
		SDL_Delay((wolfticks * 100) / 7);
}

/* -------------------------------------------------------- digitized ----- */

void SD_StopDigitized(void)
{
	DigiPlaying = false;
	DigiNumber = 0;
	DigiPriority = 0;
	SoundPositioned = false;
	if ((DigiMode == sds_PC) && (SoundMode == sdm_PC))
		SDL_SoundFinished();

	for (int c = 0; c < MIX_CHANNELS; c++) {
		chans[c].active = 0;
		channelSoundPos[c].valid = 0;
	}
}

int SD_GetChannelForDigi(int which)
{
	static int rr = 2;                  /* 0/1 reserved: player/boss weapons */

	if (DigiChannel[which] != -1)
		return DigiChannel[which];

	for (int i = 2; i < MIX_CHANNELS; i++)
		if (!chans[i].active)
			return i;
	int c = rr;                         /* all busy: steal round-robin */
	if (++rr >= MIX_CHANNELS)
		rr = 2;
	return c;
}

void SD_SetPosition(int channel, int leftpos, int rightpos)
{
	if ((leftpos < 0) || (leftpos > 15) || (rightpos < 0) || (rightpos > 15)
	        || ((leftpos == 15) && (rightpos == 15)))
		Quit("SD_SetPosition: Illegal position");

	if (channel < 0 || channel >= MIX_CHANNELS)
		return;
	/* same curve as Mix_SetPanning(((15-pos)<<4)+15): 15..255 -> 0..256 */
	chans[channel].lvol = ((15 - leftpos) << 4) + 15 + 1;
	chans[channel].rvol = ((15 - rightpos) << 4) + 15 + 1;
}

void SD_PrepareSound(int which)
{
	if (DigiList == NULL)
		Quit("SD_PrepareSound(%i): DigiList not initialized!\n", which);

	int page = DigiList[which].startpage;
	int size = DigiList[which].length;

	byte *origsamples = PM_GetSoundPage(page);
	if (origsamples + size >= PM_GetPageEnd())
		Quit("SD_PrepareSound(%i): Sound reaches out of page file!\n", which);

	/* zero-copy: mix straight from the PM page cache at play time */
	digicache[which].data = origsamples;
	digicache[which].len  = (uint32_t)size;
}

int SD_PlayDigitized(word which, int leftpos, int rightpos)
{
	if (!DigiMode)
		return 0;

	if (which >= NumDigi)
		Quit("SD_PlayDigitized: bad sound number %i", which);

	int channel = SD_GetChannelForDigi(which);
	SD_SetPosition(channel, leftpos, rightpos);

	DigiPlaying = true;

	if (!digicache[which].data) {
		printf("SD_PlayDigitized(%i): sound not prepared!\n", which);
		return 0;
	}

	chans[channel].data   = digicache[which].data;
	chans[channel].len    = digicache[which].len;
	chans[channel].pos    = 0;
	chans[channel].active = 1;

	return channel;
}

void SD_SetDigiDevice(byte mode)
{
	if (mode == DigiMode)
		return;
	SD_StopDigitized();
	if (mode != sds_SoundBlaster || SoundBlasterPresent)
		DigiMode = mode;
}

static void SDL_SetupDigi(void)
{
	/* identical to the original: parse the VSWAP sound info page */
	word *soundInfoPage = (word *)(void *)PM_GetPage(ChunksInFile - 1);
	NumDigi = (word)PM_GetPageSize(ChunksInFile - 1) / 4;

	DigiList = SafeMalloc(NumDigi * sizeof(*DigiList));
	int i, page;
	for (i = 0; i < NumDigi; i++) {
		DigiList[i].startpage = soundInfoPage[i * 2];
		if ((int)DigiList[i].startpage >= ChunksInFile - 1) {
			NumDigi = i;
			break;
		}

		int lastPage;
		if (i < NumDigi - 1) {
			lastPage = soundInfoPage[i * 2 + 2];
			if (lastPage == 0 || lastPage + PMSoundStart > ChunksInFile - 1)
				lastPage = ChunksInFile - 1;
			else
				lastPage += PMSoundStart;
		} else
			lastPage = ChunksInFile - 1;

		int size = 0;
		for (page = PMSoundStart + DigiList[i].startpage; page < lastPage; page++)
			size += PM_GetPageSize(page);

		if (lastPage == ChunksInFile - 1 && PMSoundInfoPagePadded)
			size--;

		if ((size & 0xffff0000) != 0 && (size & 0xffff) < soundInfoPage[i * 2 + 1])
			size -= 0x10000;
		size = (size & 0xffff0000) | soundInfoPage[i * 2 + 1];

		DigiList[i].length = size;
	}

	for (i = 0; i < LASTSOUND; i++) {
		DigiMap[i] = -1;
	}
	for (i = 0; i < STARTMUSIC - STARTDIGISOUNDS; i++) {
		DigiChannel[i] = -1;
		digicache[i].data = NULL;
	}
}

/* ------------------------------------------------------ mode plumbing --- */

boolean SD_SetSoundMode(byte mode)
{
	boolean result = false;
	word    tableoffset;

	SD_StopSound();

	if (mode == sdm_PC)
		mode = sdm_AdLib;               /* no PC speaker on this machine */

	switch (mode) {
	case sdm_Off:
		tableoffset = STARTADLIBSOUNDS;
		result = true;
		break;
	case sdm_AdLib:
		tableoffset = STARTADLIBSOUNDS;
		if (AdLibPresent)
			result = true;
		break;
	default:
		Quit("SD_SetSoundMode: Invalid sound mode %i", mode);
		return false;
	}
	SoundTable = &audiosegs[tableoffset];

	if (result && (mode != SoundMode)) {
		/* shut the old device */
		if (SoundMode == sdm_AdLib)
			SDL_ShutAL();
		SoundMode = mode;
		if (mode == sdm_AdLib)
			SDL_StartAL();
		SoundNumber = 0;
		SoundPriority = 0;
	}

	return result;
}

boolean SD_SetMusicMode(byte mode)
{
	boolean result = false;

	SD_FadeOutMusic();
	while (SD_MusicPlaying())
		SDL_Delay(5);

	switch (mode) {
	case smm_Off:
		result = true;
		break;
	case smm_AdLib:
		if (AdLibPresent)
			result = true;
		break;
	}

	if (result)
		MusicMode = mode;

	return result;
}

/* --------------------------------------------------------- start/stop --- */

void SD_Startup(void)
{
	if (SD_Started)
		return;

	/* one stereo 48 kHz callback stream; 512-frame granularity keeps the
	 * pump inside a display frame */
	if (rvb_audio_open(2, 512, SDW_AudioCallback, NULL) < 0) {
		printf("SD_Startup: audio open failed — running silent\n");
		return;
	}
	rvb_audio_pause(0);

	opl_reset();

	/* AdLib "present" on every flavor: register writes are a hardware
	 * no-op without FM, and the game's logic stays on its default path.
	 * SoundBlaster (digitized) is the HAL's PCM stream — always there. */
	AdLibPresent = true;
	SoundBlasterPresent = true;
	SBProPresent = false;

	alTimeCount = 0;

	SD_SetSoundMode(sdm_Off);
	SD_SetMusicMode(smm_Off);

	SDL_SetupDigi();

	SD_Started = true;
}

void SD_Shutdown(void)
{
	if (!SD_Started)
		return;

	SD_MusicOff();
	SD_StopSound();

	free(DigiList);
	DigiList = NULL;

	rvb_audio_close();

	SD_Started = false;
}

/* ------------------------------------------------------------- sfx ------ */

void SD_PositionSound(int leftvol, int rightvol)
{
	LeftPosition = leftvol;
	RightPosition = rightvol;
	nextsoundpos = true;
}

boolean SD_PlaySound(int sound)
{
	boolean     ispos;
	SoundCommon *s;
	int         lp, rp;

	lp = LeftPosition;
	rp = RightPosition;
	LeftPosition = 0;
	RightPosition = 0;

	ispos = nextsoundpos;
	nextsoundpos = false;

	if (sound == -1 || (DigiMode == sds_Off && SoundMode == sdm_Off))
		return 0;

	s = (SoundCommon *)SoundTable[sound];

	if ((SoundMode != sdm_Off) && !s)
		Quit("SD_PlaySound() - Uncached sound");

	if ((DigiMode != sds_Off) && (DigiMap[sound] != -1)) {
		int channel = SD_PlayDigitized(DigiMap[sound], lp, rp);
		SoundPositioned = ispos;
		DigiNumber = sound;
		DigiPriority = s->priority;
		return channel + 1;
	}

	if (SoundMode == sdm_Off)
		return 0;

	if (!s->length)
		Quit("SD_PlaySound() - Zero length sound");
	if (s->priority < SoundPriority)
		return 0;

	if (SoundMode == sdm_AdLib) {
		curAlSound = alSound = 0;       /* Tricob's retrigger fix */
		alOut(alFreqH, 0);
		SDL_ALPlaySound((AdLibSound *)s);
	}

	SoundNumber = sound;
	SoundPriority = s->priority;

	return 0;
}

word SD_SoundPlaying(void)
{
	if (SoundMode == sdm_AdLib && alSound)
		return SoundNumber;
	return false;
}

void SD_StopSound(void)
{
	if (DigiPlaying)
		SD_StopDigitized();

	if (SoundMode == sdm_AdLib)
		SDL_ALStopSound();

	SoundPositioned = false;

	SDL_SoundFinished();
}

void SD_WaitSoundDone(void)
{
	while (SD_SoundPlaying())
		SDL_Delay(5);
}

/* ------------------------------------------------------------- music ---- */

void SD_MusicOn(void)
{
	sqActive = true;
}

int SD_MusicOff(void)
{
	word i;

	sqActive = false;
	if (MusicMode == smm_AdLib) {
		alOut(alEffects, 0);
		for (i = 0; i < sqMaxTracks; i++)
			alOut(alFreqH + i + 1, 0);
	}

	return (int)(sqHackPtr - sqHack);
}

void SD_StartMusic(int chunk)
{
	SD_MusicOff();

	if (MusicMode == smm_AdLib) {
		int32_t chunkLen = CA_CacheAudioChunk(chunk);
		sqHack = (word *)(void *)audiosegs[chunk];
		if (*sqHack == 0)
			sqHackLen = sqHackSeqLen = chunkLen;
		else
			sqHackLen = sqHackSeqLen = *sqHack++;
		sqHackPtr = sqHack;
		sqHackTime = 0;
		alTimeCount = 0;
		SD_MusicOn();
	}
}

void SD_ContinueMusic(int chunk, int startoffs)
{
	int i;

	SD_MusicOff();

	if (MusicMode == smm_AdLib) {
		int32_t chunkLen = CA_CacheAudioChunk(chunk);
		sqHack = (word *)(void *)audiosegs[chunk];
		if (*sqHack == 0)
			sqHackLen = sqHackSeqLen = chunkLen;
		else
			sqHackLen = sqHackSeqLen = *sqHack++;
		sqHackPtr = sqHack;

		if (startoffs >= sqHackLen)
			startoffs = 0;

		/* fast forward (reconstructs the instrument registers) */
		for (i = 0; i < startoffs; i += 2) {
			byte reg = *(byte *)sqHackPtr;
			byte val = *(((byte *)sqHackPtr) + 1);
			if (reg >= 0xb1 && reg <= 0xb8)
				val &= 0xdf;            /* clear key-on */
			else if (reg == 0xbd)
				val &= 0xe0;            /* clear drum flags */

			alOut(reg, val);
			sqHackPtr += 2;
			sqHackLen -= 4;
		}
		sqHackTime = 0;
		alTimeCount = 0;

		SD_MusicOn();
	}
}

void SD_FadeOutMusic(void)
{
	if (MusicMode == smm_AdLib)
		SD_MusicOff();                  /* same quick hack as upstream */
}

boolean SD_MusicPlaying(void)
{
	return (MusicMode == smm_AdLib) ? sqActive : false;
}
