// bank.c — see bank.h. Format references: ModdingWiki "OP2 Bank Format",
// "IBK Format", "Apogee Sound System Timbre Format", "Global Timbre Library".
// SPDX-License-Identifier: BSD-2-Clause
#include "bank.h"
#include <string.h>

static uint32_t le32(const uint8_t *p)
{
	return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
	     | ((uint32_t)p[3] << 24);
}

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static void clear_bank(bank_t *b, const char *fname)
{
	memset(b, 0, sizeof(*b));
	if (fname) {
		strncpy(b->name, fname, sizeof(b->name) - 1);
		b->name[sizeof(b->name) - 1] = 0;
	}
}

// ------------------------------------------------------------------- OP2
// "#OPL_II#" + 175 x 36-byte entries (0..127 melodic, 128..174 = GM notes
// 35..81) + 175 x 32-byte names. Entry: u16 flags, u8 finetune, u8 note,
// then two 16-byte voices; we use voice 1. Voice: mod{char,AD,SR,wave,
// KSL,level}, fbcn, car{char,AD,SR,wave,KSL,level}, pad, s16 noteOffset.
#define OP2_FIXED  0x01

static void op2_voice(bank_ins_t *ins, const uint8_t *v)
{
	ins->mod20 = v[0];
	ins->mod60 = v[1];
	ins->mod80 = v[2];
	ins->modE0 = v[3] & 0x07;
	ins->mod40 = (uint8_t)((v[4] & 0xC0) | (v[5] & 0x3F));
	ins->fbcn  = v[6] & 0x0F;
	ins->car20 = v[7];
	ins->car60 = v[8];
	ins->car80 = v[9];
	ins->carE0 = v[10] & 0x07;
	ins->car40 = (uint8_t)((v[11] & 0xC0) | (v[12] & 0x3F));
	ins->note_off = (int16_t)le16(v + 14);
	if (ins->note_off < -60 || ins->note_off > 60)
		ins->note_off = 0;             // insane offset: don't trust it
}

static int load_op2(bank_t *b, const uint8_t *data, uint32_t len)
{
	if (len < 8 + 175 * 36 || memcmp(data, "#OPL_II#", 8) != 0)
		return -1;
	for (int i = 0; i < 175; i++) {
		const uint8_t *e = data + 8 + i * 36;
		bank_ins_t ins;
		memset(&ins, 0, sizeof(ins));
		op2_voice(&ins, e + 4);
		ins.present = 1;
		if (le16(e) & OP2_FIXED)
			ins.fixed_note = (uint8_t)(e[3] & 0x7F);
		if (i < BANK_NMEL) {
			b->mel[i] = ins;
		} else {                       // 128+k = percussion for note 35+k
			if (!ins.fixed_note)
				ins.fixed_note = (uint8_t)(e[3] & 0x7F);
			if (!ins.fixed_note)
				ins.fixed_note = 60;
			b->perc[i - BANK_NMEL] = ins;
		}
	}
	return 0;
}

// ------------------------------------------------------------------- IBK
// "IBK\x1A" + 128 x 16-byte SBI records + 128 x 9-byte names. Record:
// modChar carChar modScale carScale modAD carAD modSR carSR modWave carWave
// feedback percVoc transpose dPitch pad pad. Melodic bank only — the drum
// .ibk convention targets OPL rhythm mode, which this player doesn't use.
static void sbi_voice(bank_ins_t *ins, const uint8_t *v)
{
	ins->mod20 = v[0];
	ins->car20 = v[1];
	ins->mod40 = v[2];
	ins->car40 = v[3];
	ins->mod60 = v[4];
	ins->car60 = v[5];
	ins->mod80 = v[6];
	ins->car80 = v[7];
	ins->modE0 = v[8] & 0x07;
	ins->carE0 = v[9] & 0x07;
	ins->fbcn  = v[10] & 0x0F;
}

static int load_ibk(bank_t *b, const uint8_t *data, uint32_t len)
{
	if (len < 4 + 128 * 16 || memcmp(data, "IBK\x1A", 4) != 0)
		return -1;
	for (int i = 0; i < BANK_NMEL; i++) {
		const uint8_t *v = data + 4 + i * 16;
		bank_ins_t *ins = &b->mel[i];
		sbi_voice(ins, v);
		int8_t transpose = (int8_t)v[12];
		ins->note_off = (transpose >= -60 && transpose <= 60) ? transpose : 0;
		ins->present = 1;
	}
	return 0;                          // perc[] stays empty -> fallback bank
}

// ------------------------------------------------------------------- TMB
// Headerless: 256 x 13 bytes, first 12 IBK-ordered, [11] = transpose
// (melodic) / key number (percussion), [12] = velocity offset (ignored).
// Entries 128..255 are percussion indexed by MIDI note.
static int load_tmb(bank_t *b, const uint8_t *data, uint32_t len)
{
	if (len != 256 * 13)
		return -1;
	for (int i = 0; i < 256; i++) {
		const uint8_t *v = data + i * 13;
		bank_ins_t ins;
		memset(&ins, 0, sizeof(ins));
		sbi_voice(&ins, v);
		ins.present = 1;
		if (i < BANK_NMEL) {
			int8_t tr = (int8_t)v[11];
			ins.note_off = (tr >= -60 && tr <= 60) ? tr : 0;
			b->mel[i] = ins;
		} else {
			int note = i - BANK_NMEL;  // the MIDI note this entry voices
			if (note < BANK_PERC_LO || note > BANK_PERC_HI)
				continue;
			ins.fixed_note = (v[11] && v[11] < 128) ? v[11] : (uint8_t)note;
			b->perc[note - BANK_PERC_LO] = ins;
		}
	}
	return 0;
}

// ------------------------------------------------- AIL GTL (.opl / .ad)
// Directory of 6-byte {patch, bank, offset_le32} entries terminated by
// 0xFF 0xFF; each timbre: u16 length (14 = OPL2), u8 transpose, then
// mod{20,40,60,80,E0}, fbcn, car{20,40,60,80,E0}. Bank 0 = GM melodic,
// bank 127 = percussion where patch = MIDI note.
static int load_ail(bank_t *b, const uint8_t *data, uint32_t len)
{
	uint32_t pos = 0;
	int loaded = 0;
	while (pos + 2 <= len) {
		if (data[pos] == 0xFF && data[pos + 1] == 0xFF)
			break;                     // directory terminator
		if (pos + 6 > len)
			return -1;
		uint8_t  patch = data[pos];
		uint8_t  bkno  = data[pos + 1];
		uint32_t off   = le32(data + pos + 2);
		pos += 6;
		if (pos > 6 * 512)
			return -1;                 // runaway directory: not a GTL
		if (off > len || len - off < 14)
			continue;                  // skip entries pointing off the file
		const uint8_t *t = data + off;
		if (le16(t) < 14)
			continue;
		bank_ins_t ins;
		memset(&ins, 0, sizeof(ins));
		int8_t transpose = (int8_t)t[2];
		ins.mod20 = t[3];
		ins.mod40 = t[4];
		ins.mod60 = t[5];
		ins.mod80 = t[6];
		ins.modE0 = t[7] & 0x07;
		ins.fbcn  = t[8] & 0x0F;
		ins.car20 = t[9];
		ins.car40 = t[10];
		ins.car60 = t[11];
		ins.car80 = t[12];
		ins.carE0 = t[13] & 0x07;
		ins.present = 1;
		if (bkno == 0 && patch < BANK_NMEL) {
			ins.note_off = (transpose >= -60 && transpose <= 60) ? transpose : 0;
			b->mel[patch] = ins;
			loaded++;
		} else if (bkno == 127 && patch >= BANK_PERC_LO
		                       && patch <= BANK_PERC_HI) {
			ins.fixed_note = (transpose > 0 && transpose < 128)
			               ? (uint8_t)transpose : patch;
			b->perc[patch - BANK_PERC_LO] = ins;
			loaded++;
		}                              // other banks (GS variations): ignore
	}
	return loaded ? 0 : -1;
}

// ------------------------------------------------------------------ front

static int has_ext(const char *n, const char *ext)   // case-insensitive
{
	int nl = (int)strlen(n), el = (int)strlen(ext);
	if (nl < el)
		return 0;
	for (int i = 0; i < el; i++) {
		char a = n[nl - el + i], b = ext[i];
		if (a >= 'A' && a <= 'Z')
			a += 32;
		if (a != b)
			return 0;
	}
	return 1;
}

int bank_load(bank_t *b, const char *fname, const uint8_t *data, uint32_t len)
{
	if (!data || len < 16)
		return -1;
	bank_t tmp;                        // stage: only commit a good parse
	clear_bank(&tmp, fname);

	int r = -1;
	if (len >= 8 && !memcmp(data, "#OPL_II#", 8))
		r = load_op2(&tmp, data, len);
	else if (len >= 4 && !memcmp(data, "IBK\x1A", 4))
		r = load_ibk(&tmp, data, len);
	else if (len == 256 * 13)
		r = load_tmb(&tmp, data, len);
	else if (fname && (has_ext(fname, ".opl") || has_ext(fname, ".ad")))
		r = load_ail(&tmp, data, len);
	else if (fname && has_ext(fname, ".tmb"))
		r = load_tmb(&tmp, data, len);
	if (r < 0)
		return r;
	*b = tmp;
	return 0;
}

void bank_builtin(bank_t *b)
{
	clear_bank(b, "(built-in)");
	bank_ins_t pno = {                 // plain FM piano, every program
		.present = 1,
		.mod20 = 0x01, .mod40 = 0x18, .mod60 = 0xF4, .mod80 = 0x47, .modE0 = 0,
		.car20 = 0x01, .car40 = 0x00, .car60 = 0xF2, .car80 = 0x47, .carE0 = 0,
		.fbcn = 0x00,
	};
	bank_ins_t drum = {                // short noisy thunk, every drum note
		.present = 1, .fixed_note = 60,
		.mod20 = 0x0C, .mod40 = 0x00, .mod60 = 0xF8, .mod80 = 0xB5, .modE0 = 0,
		.car20 = 0x01, .car40 = 0x00, .car60 = 0xF6, .car80 = 0x94, .carE0 = 0,
		.fbcn = 0x0E,
	};
	for (int i = 0; i < BANK_NMEL; i++)
		b->mel[i] = pno;
	for (int i = 0; i < BANK_NPERC; i++)
		b->perc[i] = drum;
}
