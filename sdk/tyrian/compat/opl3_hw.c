/*
 * opl3_hw.c — Tyrian's AdLib music on the FM flavor's REAL OPL3.
 *
 * Same seam as Carlos's RetroWave build (opentyrian2000-retrowave: every
 * adlib_write mirrored to a hardware OPL3, emulator muted), minus the
 * emulator entirely: lds_play.c is an integer sequencer, the chip does the
 * synthesis, and core_top mixes FM into the DAC after our PCM — so
 * adlib_getsample() just returns silence for the SFX mix to ride on.
 * Zero DSP on the CPU; rv32im never touches a float.
 *
 * OPL2-program-on-OPL3 notes:
 *  - We run the chip in OPL3 mode (0x105 NEW=1) because that is the mode the
 *    flavor's synth is validated in (fmdemo). In NEW mode the 0xC0 channel
 *    registers gain L/R output-enable bits that OPL2 programs leave at 0
 *    (= silence), so 0xC0-0xC8 writes get OR'd with 0x30 (both speakers).
 *  - On flavors without FM (sys_caps), every call is a no-op and
 *    music_disabled stays true (see opentyr.c) — one binary, any flavor.
 *
 * GPL-2.0-or-later (port glue; see compat/SDL.h).
 */
#include "hal.h"                /* opl_write, sys_caps — BEFORE opl.h: that
                                 * header #defines opl_write for the emulator
                                 * and would mangle hal.h's declaration */
#include "lds_play.h"
#include "opl.h"
#undef opl_write                 /* we call the HAL's real one */

#include <string.h>

static uint8_t shadow[256];             /* OPL2 register file mirror */
static int     have_fm = -1;

static int fm(void)
{
	if (have_fm < 0)
		have_fm = (sys_caps()->features & HAL_FEAT_FM) ? 1 : 0;
	return have_fm;
}

static void hw_write(unsigned idx, uint8_t val)
{
	if ((idx & 0xF0) == 0xC0)
		val |= 0x30;                    /* L+R enable (NEW-mode semantics) */
	opl_write((uint16_t)idx, val);
}

void adlib_init(Bit32u samplerate)
{
	(void)samplerate;                   /* synthesis is silicon; rate is N/A */
	memset(shadow, 0, sizeof(shadow));
	if (!fm())
		return;
	/* silence + neutral state across the OPL2 register file */
	for (unsigned r = 0x20; r <= 0xF5; r++)
		hw_write(r, 0);
	opl_write(0x105, 0x01);             /* NEW=1: the validated mode */
	opl_write(0x104, 0x00);             /* no 4-op pairings */
	opl_write(0x08,  0x00);
	opl_write(0xBD,  0x00);             /* melodic mode */
}

void adlib_write(Bitu idx, Bit8u val)
{
	if (idx < sizeof(shadow))
		shadow[idx] = val;
	if (fm())
		hw_write((unsigned)idx, val);
}

/* lds_play peeks at 0xBD for rhythm-mode bookkeeping */
Bitu adlib_reg_read(Bitu port)
{
	return (port < sizeof(shadow)) ? shadow[port] : 0;
}

void adlib_write_index(Bitu port, Bit8u val)
{
	(void)port; (void)val;              /* two-step port iface unused by LDS */
}

/* The FM voice is mixed into the DAC in hardware AFTER our PCM stream, so
 * the "music samples" contribution to the software mix is silence. This
 * call is still the music clock: loudness.c counts these samples to pace
 * lds_update() at the song's Loudness rate. */
void adlib_getsample(Bit16s *sndptr, Bits numsamples)
{
	memset(sndptr, 0, (size_t)numsamples * sizeof(Bit16s));
}
