/*
 * id_sd_rvstack.c — Omnispeak SD_Backend on the riscv-stack HAL.
 *
 * Keen's audio is AdLib (OPL2 register programs: IMF-style music through
 * SD_AL_MusicService, register-list SFX through SD_AL_SoundService) plus a
 * PC-speaker fallback. The design thesis holds: the CPU never synthesizes
 * FM — alOut() forwards every register byte to the FM flavor's REAL OPL3
 * via opl_write(), gated on sys_caps()->features & HAL_FEAT_FM. On the PC
 * twin the same stream lands in $RVSTACK_OPLLOG (with RVSTACK_FORCE_FM=1).
 *
 * OPL2-program-on-OPL3 notes (the Tyrian/Wolf3D lesson):
 *  - The chip runs in OPL3 NEW mode (0x105 bit0) — the mode the flavor's
 *    synth is validated in. NEW mode adds L/R output-enable bits to the
 *    0xC0 channel registers that OPL2 programs leave 0 (= silence), so
 *    0xC0-0xC8 writes get OR'd with 0x30 (both speakers).
 *
 * Timing: vanilla Keen reprograms PIT timer 0 to 140 Hz (SFX only) or
 * 560 Hz (music on) and runs SDL_t0Service from the interrupt. This
 * platform has NO threads and NO timer interrupts for games — instead
 * RVK_TimerPump() (called from every present / pumpEvents / wait loop)
 * converts elapsed sys_ticks_us microseconds into elapsed PIT ticks and
 * runs the service the right number of times. Sample-exact enough for
 * IMF playback (the same batching wolf3d's audio callback does).
 *
 * PC speaker: stubbed (there is no beeper; sdm_PC selects fine but is
 * silent). No digitized audio exists in Keen — the PCM stream is unused.
 *
 * Part of the Omnispeak riscv-stack port glue. SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hal.h" /* FIRST (trap #2) */
#include "rv_keen.h"

#include <stdbool.h>
#include <stdint.h>

#include "id_sd.h"

void SDL_t0Service(void); /* id_sd.c: the 140/560 Hz sequencer */

#define PC_PIT_RATE 1193182u

/* ------------------------------------------------------------- state -- */

static volatile int sd_rv_timerDivisor = 8514; /* 1192030/140: safe default */
static uint32_t sd_rv_lastUs;
static uint64_t sd_rv_pitAccum; /* PIT ticks not yet consumed */
static int sd_rv_up;
static int sd_rv_inPump;

static uint8_t sd_rv_shadow[256];
static int sd_rv_haveFM = -1;

static int SD_RV_HaveFM(void)
{
	if (sd_rv_haveFM < 0)
		sd_rv_haveFM = (sys_caps()->features & HAL_FEAT_FM) ? 1 : 0;
	return sd_rv_haveFM;
}

/* ------------------------------------------------------------ timing -- */

void RVK_TimerPump(void)
{
	if (!sd_rv_up || sd_rv_inPump)
		return;
	sd_rv_inPump = 1; /* t0Service code paths can present/pump: no re-entry */

	uint32_t now = sys_ticks_us();
	uint32_t delta = now - sd_rv_lastUs; /* wrap-safe unsigned math */
	sd_rv_lastUs = now;
	if (delta > 1000000u)
		delta = 1000000u; /* clamp a stall to 1 s of catch-up */

	sd_rv_pitAccum += (uint64_t)delta * PC_PIT_RATE / 1000000u;

	int div = sd_rv_timerDivisor > 0 ? sd_rv_timerDivisor : 8514;
	uint32_t n = (uint32_t)(sd_rv_pitAccum / (uint32_t)div);
	sd_rv_pitAccum -= (uint64_t)n * (uint32_t)div;
	if (n > 600) /* never spiral (600 = ~1 s at the music rate) */
		n = 600;
	while (n--)
		SDL_t0Service();

	sd_rv_inPump = 0;
}

static void SD_RV_SetTimer0(int16_t int_8_divisor)
{
	sd_rv_timerDivisor = int_8_divisor;
}

/* --------------------------------------------------- the OPL3 seam -- */

static void SD_RV_alOut(uint8_t reg, uint8_t val)
{
	sd_rv_shadow[reg] = val;
	if (!SD_RV_HaveFM())
		return;
	if ((reg & 0xF0) == 0xC0)
		val |= 0x30; /* L+R enable (OPL3 NEW-mode semantics) */
	opl_write(reg, val);
}

static void SD_RV_ResetOPL3(void)
{
	if (!SD_RV_HaveFM())
		return;
	/* silence + defined state across all OPL2 regs, then NEW mode */
	for (int r = 0x20; r <= 0xF5; r++)
	{
		uint8_t v = 0;
		if ((r & 0xF0) == 0xC0)
			v = 0x30;
		opl_write((uint16_t)r, v);
	}
	opl_write(0x105, 0x01); /* NEW=1: the validated mode */
	opl_write(0x104, 0x00); /* no 4-op pairings */
	opl_write(0x08, 0x00);
	opl_write(0xBD, 0x00); /* melodic mode */
	opl_write(0x01, 0x20); /* wave select enable (vanilla SD does too) */
}

/* ------------------------------------------------------------ backend -- */

static void SD_RV_Startup(void)
{
	SD_RV_ResetOPL3();
	sd_rv_lastUs = sys_ticks_us();
	sd_rv_pitAccum = 0;
	sd_rv_up = 1;
	RVK_Beacon(3);
}

static void SD_RV_Shutdown(void)
{
	sd_rv_up = 0;
	SD_RV_ResetOPL3();
}

/* Single-threaded: the "interrupt" only ever runs inside RVK_TimerPump,
 * never concurrently with game code — lock/unlock are no-ops. */
static void SD_RV_Lock(void) {}
static void SD_RV_Unlock(void) {}

static void SD_RV_PCSpkOn(bool on, int freq)
{
	(void)on;
	(void)freq; /* no beeper on this console (documented stub) */
}

static void SD_RV_WaitTick(void)
{
	uint32_t t = SD_GetTimeCount();
	while (SD_GetTimeCount() == t)
	{
		RVK_TimerPump();
		fb_flip_poll();
		sys_delay_us(500);
	}
}

static unsigned int SD_RV_Detect(void)
{
	/* Report AdLib on every flavor: one binary — on non-FM consoles
	 * opl_write is a hardware no-op and the game simply plays silent. */
	return SD_CARD_OPL2;
}

static void SD_RV_SetOPL3(bool on)
{
	(void)on; /* we always run NEW mode with the 0xC0 L/R fix */
}

SD_Backend sd_rvstack_backend = {
	.startup = SD_RV_Startup,
	.shutdown = SD_RV_Shutdown,
	.lock = SD_RV_Lock,
	.unlock = SD_RV_Unlock,
	.alOut = SD_RV_alOut,
	.pcSpkOn = SD_RV_PCSpkOn,
	.setTimer0 = SD_RV_SetTimer0,
	.waitTick = SD_RV_WaitTick,
	.detect = SD_RV_Detect,
	.setOPL3 = SD_RV_SetOPL3,
};

SD_Backend *SD_Impl_GetBackend()
{
	return &sd_rvstack_backend;
}
