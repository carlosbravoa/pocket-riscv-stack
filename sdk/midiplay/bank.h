// bank — OPL patch banks from four classic file formats, normalized.
//
// The synth core (oplgm.c) is bank-agnostic: every loader fills the same
// bank_ins_t (raw OPL register images for one 2-op voice, plus pitch info).
// Supported bank file formats (detected by magic first, extension second):
//
//   .op2        DMX GENMIDI (Doom): "#OPL_II#", 128 melodic + 47 percussion,
//               double-voice entries use voice 1 only (documented tradeoff).
//   .ibk        Creative SBTimbre bank: "IBK\x1A", 128 melodic instruments.
//               (Drum .ibk variants aren't note-mapped — loaded as melodic.)
//   .tmb        Apogee Sound System timbres: 256 x 13 bytes, headerless;
//               128 melodic + 128 percussion keyed by MIDI note.
//   .opl / .ad  AIL/Miles Global Timbre Library (e.g. the classic fat.opl):
//               {patch,bank,offset} directory, bank 0 melodic / 127 drums.
//
// All parsers are defensive: every offset is bounds-checked, a bad file is
// rejected with an error (the previous bank stays active), never a crash.
//
// SPDX-License-Identifier: BSD-2-Clause
#ifndef MIDIPLAY_BANK_H
#define MIDIPLAY_BANK_H

#include <stdint.h>

typedef struct {
	uint8_t present;                   // 0 = hole (fall back / skip)
	uint8_t fixed_note;                // percussion pitch; 0 = use MIDI note
	int16_t note_off;                  // semitone offset added to the note
	uint8_t mod20, mod40, mod60, mod80, modE0;   // modulator register images
	uint8_t car20, car40, car60, car80, carE0;   // carrier register images
	uint8_t fbcn;                      // feedback/connection (0xC0 low bits)
} bank_ins_t;

#define BANK_NMEL   128
#define BANK_PERC_LO 35                // GM percussion note range
#define BANK_PERC_HI 81
#define BANK_NPERC  (BANK_PERC_HI - BANK_PERC_LO + 1)

typedef struct {
	bank_ins_t mel[BANK_NMEL];
	bank_ins_t perc[BANK_NPERC];       // indexed by MIDI note - BANK_PERC_LO
	char       name[48];               // file name, for the UI
} bank_t;

// 0 = loaded; <0 = not a recognizable/sane bank (bank untouched on error).
int bank_load(bank_t *b, const char *fname, const uint8_t *data, uint32_t len);

// Built-in last-resort patches (simple FM piano + drum thunk) so the player
// still makes sound if a pak ships with no banks/ at all.
void bank_builtin(bank_t *b);

#endif // MIDIPLAY_BANK_H
