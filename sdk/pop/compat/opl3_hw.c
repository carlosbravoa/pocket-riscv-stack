/*
 * opl3_hw.c — replaces SDLPoP's Nuked OPL3 software synth (src/opl3.c) with a
 * forwarder to the RISC-V Stack's HARDWARE OPL3. midi.c is unchanged: it still
 * calls OPL3_WriteReg / OPL3_Reset / OPL3_GenerateStream; here those forward
 * register writes to opl_write() (FM-gated) and generate SILENCE. Zero CPU
 * synthesis — the FM flavor's opl3_fpga does the work. (Tyrian's opl3_hw.c
 * pattern.) A non-FM flavor: writes are harmless no-ops, music is silent.
 *
 * SPDX-License-Identifier: BSD-2-Clause (shim); midi.c is GPL-3.0-or-later.
 */
#include "hal.h"
#include "opl3.h"                /* struct _opl3_chip + Bit* types + protos */

#include <string.h>

static int fm_present(void)
{
	static int cached = -1;
	if (cached < 0) cached = (sys_caps()->features & HAL_FEAT_FM) ? 1 : 0;
	return cached;
}

static void hw_write(Bit16u reg, Bit8u v)
{
	if (!fm_present()) return;
	/* OPL3 NEW mode: channel regs 0xC0-0xC8 (and bank-1 0x1C0-0x1C8) carry the
	 * L/R output enables in bits 4-5; the software synth defaults them off.
	 * OR 0x30 so both speakers are driven. */
	if ((reg & 0xF0) == 0xC0) v |= 0x30;
	opl_write(reg, v);
}

void OPL3_WriteReg(opl3_chip *chip, Bit16u reg, Bit8u v)
{
	(void)chip;
	hw_write(reg, v);
}

/* midi.c also uses the "buffered" variant; hardware is synchronous, so forward
 * immediately (opl_write already handles CDC pacing + the retrigger guard). */
void OPL3_WriteRegBuffered(opl3_chip *chip, Bit16u reg, Bit8u v)
{
	(void)chip;
	hw_write(reg, v);
}

void OPL3_Reset(opl3_chip *chip, Bit32u samplerate)
{
	(void)samplerate;
	if (chip) memset(chip, 0, sizeof *chip);
	if (!fm_present()) return;
	opl_write(0x105, 0x01);          /* OPL3 NEW mode on */
	opl_write(0x104, 0x00);          /* no 4-op connections */
	for (Bit16u r = 0x40; r <= 0x55; r++) { opl_write(r, 0x3F); opl_write(0x100|r, 0x3F); }
	for (Bit16u r = 0xA0; r <= 0xB8; r++) { opl_write(r, 0x00); opl_write(0x100|r, 0x00); }
	for (Bit16u r = 0xC0; r <= 0xC8; r++) { opl_write(r, 0x30); opl_write(0x100|r, 0x30); }
}

/* The three generate entry points return silence — the register writes above
 * already reached the FPGA chip. midi.c's sample counting/pacing is preserved. */
void OPL3_Generate(opl3_chip *chip, Bit16s *buf) { (void)chip; buf[0] = buf[1] = 0; }
void OPL3_GenerateResampled(opl3_chip *chip, Bit16s *buf) { (void)chip; buf[0] = buf[1] = 0; }
void OPL3_GenerateStream(opl3_chip *chip, Bit16s *sndptr, Bit32u numsamples)
{
	(void)chip;
	memset(sndptr, 0, (size_t)numsamples * 2 * sizeof(Bit16s));  /* stereo */
}
