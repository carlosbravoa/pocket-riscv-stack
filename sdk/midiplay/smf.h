// smf — clean-room Standard MIDI File reader (format 0 and 1, integer-only).
//
// Zero-copy: the file stays wherever it is (pakfs DRAM); the reader walks it
// with per-track cursors and merges tracks on the fly in tick order.  All
// reads are bounds-checked; a malformed track is dropped, never followed off
// the end.  Timing stays in TICKS here — the player converts to microseconds
// with the tempo events this iterator delivers in stream order.
//
// SPDX-License-Identifier: BSD-2-Clause
#ifndef MIDIPLAY_SMF_H
#define MIDIPLAY_SMF_H

#include <stdint.h>

#define SMF_MAX_TRACKS 24              // extra tracks are ignored, not fatal

typedef struct {
	const uint8_t *p, *end;            // cursor into the track chunk
	uint32_t next_tick;                // absolute tick of the event at p
	uint8_t  running;                  // running-status byte (0 = none)
	uint8_t  done;
} smf_track_t;

typedef struct {
	const uint8_t *data;
	uint32_t len;
	uint16_t format;                   // 0 or 1
	uint16_t ntracks;                  // tracks actually mounted
	uint16_t division;                 // ticks per quarter note (PPQN)
	smf_track_t trk[SMF_MAX_TRACKS];
} smf_t;

typedef struct {
	uint32_t tick;                     // absolute ticks from song start
	uint8_t  status;                   // 0x80..0xE0 (channel in low nibble),
	                                   // or 0xFF = tempo change
	uint8_t  a, b;                     // data bytes (b = 0 where unused)
	uint32_t tempo;                    // us per quarter (status == 0xFF only)
} smf_evt_t;

// 0 = ok; -1 = not an SMF / unsupported format; -2 = SMPTE timing (rare,
// unsupported); -3 = no usable tracks.  Never crashes on garbage.
int  smf_open(smf_t *s, const uint8_t *data, uint32_t len);
void smf_rewind(smf_t *s);             // back to tick 0 (cheap)
int  smf_next(smf_t *s, smf_evt_t *e); // 1 = event out, 0 = end of song

#endif // MIDIPLAY_SMF_H
