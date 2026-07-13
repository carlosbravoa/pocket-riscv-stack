// oplgm.c — see oplgm.h. Vendored verbatim from sdk/midiplay (branch
// app/midiplayer, where tools/gen_tables.py lives); keep in sync there.
// Only the tables include is renamed (opltables.h — Doom owns tables.h).
// All integer; every float was precomputed into the tables.
//
// OPL3 notes (mirrors sdk/tyrian/compat/opl3_hw.c's findings): the chip runs
// in NEW mode (0x105 = 1) because that is the mode the FM flavor validates;
// in NEW mode the 0xC0 registers carry L/R output enables, so every 0xC0
// write sets bits 4-5. Channels 0-8 live in register bank 0, channels 9-17
// at the same offsets + 0x100. opl_write() itself handles retrigger pacing.
//
// SPDX-License-Identifier: BSD-2-Clause
#include "oplgm.h"
#include "hal.h"
#include "opltables.h"
#include <string.h>

// operator offsets for channels 0..8 within a register bank
static const uint8_t op_off[9] = { 0x00,0x01,0x02,0x08,0x09,0x0A,0x10,0x11,0x12 };

typedef struct {
	uint8_t  on, sustained;
	uint8_t  midich, note, vel;
	const bank_ins_t *ins;             // patch this voice is set up for
	int32_t  pitch;                    // note incl. patch offset, 1/32 semi
	uint8_t  b0;                       // last 0xB0 image (for key-off)
	uint32_t age;                      // allocation stamp (steal oldest)
} vox_t;

typedef struct {
	uint8_t prog, vol, expr, pan, sustain;
	int16_t bend;                      // -8192..8191
} chan_t;

static vox_t   vox[OPLGM_NVOICE];
static chan_t  chan[16];
static uint32_t stamp;
static const bank_t *bank_pri, *bank_fb;
static uint8_t shadow[2][256];         // written register images
static uint8_t shadow_ok[2][256];      // 0 = unknown (force first write)

static void wr(int bank, uint8_t reg, uint8_t val)
{
	if (shadow_ok[bank][reg] && shadow[bank][reg] == val)
		return;                        // redundant: keep the bus/log quiet
	shadow[bank][reg] = val;
	shadow_ok[bank][reg] = 1;
	opl_write((uint16_t)((bank << 8) | reg), val);
}

static void chan_defaults(int c)
{
	chan[c].prog = 0;
	chan[c].vol = 100;                 // GM power-on defaults
	chan[c].expr = 127;
	chan[c].pan = 64;
	chan[c].sustain = 0;
	chan[c].bend = 0;
}

void oplgm_init(void)
{
	memset(vox, 0, sizeof(vox));
	memset(shadow_ok, 0, sizeof(shadow_ok));
	stamp = 0;
	for (int c = 0; c < 16; c++)
		chan_defaults(c);

	opl_write(0x105, 0x01);            // NEW=1 first: unlocks bank 1 + L/R
	opl_write(0x104, 0x00);            // no 4-op pairs: 18 x 2-op
	opl_write(0x01,  0x20);            // wave select enable (OPL2 courtesy)
	opl_write(0x08,  0x00);
	opl_write(0xBD,  0x00);            // melodic mode, no rhythm section
	for (int v = 0; v < OPLGM_NVOICE; v++) {
		int bk = v / 9, ch = v % 9;
		wr(bk, (uint8_t)(0x40 + op_off[ch]), 0x3F);       // both ops silent
		wr(bk, (uint8_t)(0x40 + op_off[ch] + 3), 0x3F);
		wr(bk, (uint8_t)(0xB0 + ch), 0x00);               // keyed off
		wr(bk, (uint8_t)(0xC0 + ch), 0x30);               // L+R on
	}
}

void oplgm_set_bank(const bank_t *primary, const bank_t *fallback)
{
	oplgm_all_off();
	bank_pri = primary;
	bank_fb  = fallback;
	for (int v = 0; v < OPLGM_NVOICE; v++)
		vox[v].ins = 0;                // stale patches: force re-program
}

// ---------------------------------------------------------------- lookup

static const bank_ins_t *find_ins(int ch, int note)
{
	if (ch == 9) {                     // MIDI channel 10: percussion
		if (note < BANK_PERC_LO || note > BANK_PERC_HI)
			return 0;
		int i = note - BANK_PERC_LO;
		if (bank_pri && bank_pri->perc[i].present)
			return &bank_pri->perc[i];
		if (bank_fb && bank_fb->perc[i].present)
			return &bank_fb->perc[i];
		return 0;
	}
	int p = chan[ch].prog & 0x7F;
	if (bank_pri && bank_pri->mel[p].present)
		return &bank_pri->mel[p];
	if (bank_fb && bank_fb->mel[p].present)
		return &bank_fb->mel[p];
	return 0;
}

// ---------------------------------------------------------------- voicing

static void load_patch(int v, const bank_ins_t *ins)
{
	int bk = v / 9, ch = v % 9;
	uint8_t m = op_off[ch], c = (uint8_t)(m + 3);
	wr(bk, (uint8_t)(0x20 + m), ins->mod20);
	wr(bk, (uint8_t)(0x60 + m), ins->mod60);
	wr(bk, (uint8_t)(0x80 + m), ins->mod80);
	wr(bk, (uint8_t)(0xE0 + m), ins->modE0);
	wr(bk, (uint8_t)(0x20 + c), ins->car20);
	wr(bk, (uint8_t)(0x60 + c), ins->car60);
	wr(bk, (uint8_t)(0x80 + c), ins->car80);
	wr(bk, (uint8_t)(0xE0 + c), ins->carE0);

	// pan: NEW-mode L/R enables ride the feedback/connection register
	uint8_t pan = chan[vox[v].midich].pan;
	uint8_t lr = (pan < 43) ? 0x10 : (pan > 85) ? 0x20 : 0x30;
	wr(bk, (uint8_t)(0xC0 + ch), (uint8_t)((ins->fbcn & 0x0F) | lr));
	vox[v].ins = ins;
}

static void set_volume(int v)
{
	const bank_ins_t *ins = vox[v].ins;
	if (!ins)
		return;
	int bk = v / 9, ch = v % 9;
	chan_t *C = &chan[vox[v].midich];
	// dB-domain sum of velocity, channel volume, expression attenuations
	int att = vol_atten[vox[v].vel & 0x7F] + vol_atten[C->vol & 0x7F]
	        + vol_atten[C->expr & 0x7F];
	int car = (ins->car40 & 0x3F) + att;
	if (car > 63)
		car = 63;
	wr(bk, (uint8_t)(0x40 + op_off[ch] + 3),
	   (uint8_t)((ins->car40 & 0xC0) | car));
	int mod = ins->mod40 & 0x3F;
	if (ins->fbcn & 0x01) {            // additive: modulator is audible too
		mod += att;
		if (mod > 63)
			mod = 63;
	}
	wr(bk, (uint8_t)(0x40 + op_off[ch]), (uint8_t)((ins->mod40 & 0xC0) | mod));
}

static void set_freq(int v, int keyon)
{
	int bk = v / 9, ch = v % 9;
	// pitch in 1/32 semitones incl. patch note offset; add pitch bend
	// (+/- 2 semitones full scale = +/- 64 steps)
	int32_t p = vox[v].pitch + ((int32_t)chan[vox[v].midich].bend * 64) / 8192;
	int32_t lo = 12 * FNUM_STEPS;                       // MIDI note 12
	int32_t hi = lo + 8 * 12 * FNUM_STEPS - 1;          // 8 blocks up
	if (p < lo)
		p = lo;
	if (p > hi)
		p = hi;
	int block = (p - lo) / (12 * FNUM_STEPS);
	int idx   = (p - lo) % (12 * FNUM_STEPS);
	uint16_t fnum = fnum_fine[idx];
	uint8_t b0 = (uint8_t)((keyon ? 0x20 : 0x00) | ((block & 7) << 2)
	                       | ((fnum >> 8) & 3));
	wr(bk, (uint8_t)(0xA0 + ch), (uint8_t)(fnum & 0xFF));
	wr(bk, (uint8_t)(0xB0 + ch), b0);
	vox[v].b0 = b0;
}

static void key_off(int v)
{
	int bk = v / 9, ch = v % 9;
	wr(bk, (uint8_t)(0xB0 + ch), (uint8_t)(vox[v].b0 & ~0x20));
	vox[v].b0 &= (uint8_t)~0x20;
	vox[v].on = 0;
	vox[v].sustained = 0;
}

static int alloc_voice(void)
{
	int best = -1;
	uint32_t best_age = 0xFFFFFFFFu;
	for (int v = 0; v < OPLGM_NVOICE; v++)     // 1st choice: an idle voice
		if (!vox[v].on && !vox[v].sustained && vox[v].age < best_age) {
			best = v;
			best_age = vox[v].age;
		}
	if (best >= 0)
		return best;
	for (int v = 0; v < OPLGM_NVOICE; v++)     // 2nd: oldest sustained
		if (vox[v].sustained && vox[v].age < best_age) {
			best = v;
			best_age = vox[v].age;
		}
	if (best < 0) {
		best_age = 0xFFFFFFFFu;                // 3rd: steal the oldest note
		for (int v = 0; v < OPLGM_NVOICE; v++)
			if (vox[v].age < best_age) {
				best = v;
				best_age = vox[v].age;
			}
	}
	key_off(best);
	return best;
}

// ---------------------------------------------------------------- MIDI in

void oplgm_note_on(int ch, int note, int vel)
{
	if (ch < 0 || ch > 15 || note < 0 || note > 127)
		return;
	if (vel == 0) {
		oplgm_note_off(ch, note);
		return;
	}
	const bank_ins_t *ins = find_ins(ch, note);
	if (!ins)
		return;                        // hole in the bank: skip, don't crash

	int v = alloc_voice();
	vox[v].on = 1;
	vox[v].sustained = 0;
	vox[v].midich = (uint8_t)ch;
	vox[v].note = (uint8_t)note;
	vox[v].vel = (uint8_t)(vel & 0x7F);
	vox[v].age = ++stamp;

	int base = ins->fixed_note ? ins->fixed_note : note;
	vox[v].pitch = (base + ins->note_off) * FNUM_STEPS;

	if (vox[v].ins != ins)
		load_patch(v, ins);
	else {                             // same patch: pan may still change
		int bk = v / 9, hw = v % 9;
		uint8_t pan = chan[ch].pan;
		uint8_t lr = (pan < 43) ? 0x10 : (pan > 85) ? 0x20 : 0x30;
		wr(bk, (uint8_t)(0xC0 + hw), (uint8_t)((ins->fbcn & 0x0F) | lr));
	}
	set_volume(v);
	set_freq(v, 1);                    // writes 0xB0 with KON last
}

void oplgm_note_off(int ch, int note)
{
	for (int v = 0; v < OPLGM_NVOICE; v++) {
		if (!vox[v].on || vox[v].midich != ch || vox[v].note != note)
			continue;
		if (chan[ch].sustain)
			vox[v].sustained = 1;      // pedal holds it; release on CC64=0
		else
			key_off(v);
		vox[v].on = 0;
		return;                        // one note-off releases one voice
	}
}

void oplgm_program(int ch, int prog)
{
	if (ch >= 0 && ch <= 15)
		chan[ch].prog = (uint8_t)(prog & 0x7F);
}

void oplgm_bend(int ch, int bend14)
{
	if (ch < 0 || ch > 15)
		return;
	chan[ch].bend = (int16_t)((bend14 & 0x3FFF) - 8192);
	for (int v = 0; v < OPLGM_NVOICE; v++)
		if ((vox[v].on || vox[v].sustained) && vox[v].midich == ch)
			set_freq(v, vox[v].b0 >> 5 & 1);
}

static void release_sustained(int ch)
{
	for (int v = 0; v < OPLGM_NVOICE; v++)
		if (vox[v].sustained && vox[v].midich == ch)
			key_off(v);
}

void oplgm_control(int ch, int cc, int val)
{
	if (ch < 0 || ch > 15)
		return;
	switch (cc) {
	case 7:
		chan[ch].vol = (uint8_t)(val & 0x7F);
		goto revolume;
	case 11:
		chan[ch].expr = (uint8_t)(val & 0x7F);
		goto revolume;
	case 10:
		chan[ch].pan = (uint8_t)(val & 0x7F);
		return;                        // applied on the next note
	case 64:
		chan[ch].sustain = (val >= 64);
		if (!chan[ch].sustain)
			release_sustained(ch);
		return;
	case 121:                          // reset all controllers
		chan[ch].expr = 127;
		chan[ch].bend = 0;
		chan[ch].sustain = 0;
		release_sustained(ch);
		return;
	case 120:                          // all sound off
	case 123:                          // all notes off
		for (int v = 0; v < OPLGM_NVOICE; v++)
			if ((vox[v].on || vox[v].sustained) && vox[v].midich == ch)
				key_off(v);
		return;
	default:
		return;
	}
revolume:
	for (int v = 0; v < OPLGM_NVOICE; v++)
		if ((vox[v].on || vox[v].sustained) && vox[v].midich == ch)
			set_volume(v);
}

void oplgm_all_off(void)
{
	for (int v = 0; v < OPLGM_NVOICE; v++)
		key_off(v);
	for (int c = 0; c < 16; c++)
		chan[c].sustain = 0;
}

int oplgm_voice_note(int v, int *midi_ch)
{
	if (v < 0 || v >= OPLGM_NVOICE || (!vox[v].on && !vox[v].sustained))
		return -1;
	if (midi_ch)
		*midi_ch = vox[v].midich;
	return vox[v].note;
}
