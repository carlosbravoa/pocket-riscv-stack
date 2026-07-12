// oplgm — a General MIDI performer on the OPL3: 16 MIDI channels in,
// opl_write() register stream out. Bank-agnostic (see bank.h).
//
//   18 two-op melodic voices (OPL3 NEW mode, both register banks),
//   oldest-note stealing, percussion on MIDI channel 10 via dedicated
//   patches (melodic-mode drums — no OPL rhythm mode).
//
// SPDX-License-Identifier: BSD-2-Clause
#ifndef MIDIPLAY_OPLGM_H
#define MIDIPLAY_OPLGM_H

#include <stdint.h>
#include "bank.h"

#define OPLGM_NVOICE 18

void oplgm_init(void);                          // chip to a known state
// Primary bank + fallback for entries the primary lacks (e.g. an .ibk has
// no percussion). Switching re-voices cleanly: all notes off, patch caches
// invalidated; playback continues and new notes sound in the new bank.
void oplgm_set_bank(const bank_t *primary, const bank_t *fallback);

void oplgm_note_on(int ch, int note, int vel);  // vel 0 == note off (SMF law)
void oplgm_note_off(int ch, int note);
void oplgm_program(int ch, int prog);
void oplgm_control(int ch, int cc, int val);    // 7/10/11/64/120/121/123
void oplgm_bend(int ch, int bend14);            // 0..16383, center 8192
void oplgm_all_off(void);                       // hard stop, every voice

// UI probes: the note a hardware voice is sounding (-1 = idle) and a
// per-MIDI-channel activity level (0..127, decayed by the caller's clock).
int  oplgm_voice_note(int v, int *midi_ch);

#endif // MIDIPLAY_OPLGM_H
