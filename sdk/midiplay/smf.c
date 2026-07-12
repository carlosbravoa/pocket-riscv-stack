// smf.c — clean-room Standard MIDI File reader. See smf.h.
//
// Design: no allocation, no copies, no floats. Each track keeps a cursor;
// smf_next() repeatedly picks the track with the earliest pending tick and
// decodes ONE event from it. Events the player has no use for (sysex, most
// metas) are consumed internally. Anything malformed ends that one track.
//
// SPDX-License-Identifier: BSD-2-Clause
#include "smf.h"

// ---------------------------------------------------------------- helpers

static uint32_t be32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
	     | ((uint32_t)p[2] << 8) | p[3];
}

static uint32_t be16(const uint8_t *p) { return ((uint32_t)p[0] << 8) | p[1]; }

// Variable-length quantity. Returns 0 on overrun/overlength (track dies).
static int read_vlq(smf_track_t *t, uint32_t *out)
{
	uint32_t v = 0;
	for (int i = 0; i < 4; i++) {
		if (t->p >= t->end)
			return 0;
		uint8_t b = *t->p++;
		v = (v << 7) | (b & 0x7F);
		if (!(b & 0x80)) {
			*out = v;
			return 1;
		}
	}
	return 0;                          // >4 bytes: not a legal VLQ
}

// Read the delta of the next event and update the track's absolute tick.
static void arm_track(smf_track_t *t)
{
	uint32_t dt;
	if (!read_vlq(t, &dt)) {
		t->done = 1;
		return;
	}
	t->next_tick += dt;
}

// ---------------------------------------------------------------- open

int smf_open(smf_t *s, const uint8_t *data, uint32_t len)
{
	s->data = data;
	s->len  = len;
	s->ntracks = 0;

	if (!data || len < 14 || be32(data) != 0x4D546864u)     // "MThd"
		return -1;
	uint32_t hlen = be32(data + 4);
	if (hlen < 6 || 8 + hlen > len)
		return -1;
	s->format   = (uint16_t)be16(data + 8);
	uint32_t declared = be16(data + 10);
	uint32_t division = be16(data + 12);
	if (s->format > 1)
		return -1;                     // format 2 is a patch-bay, not a song
	if (division & 0x8000)
		return -2;                     // SMPTE timing: not supported
	if (division == 0)
		return -1;
	s->division = (uint16_t)division;

	// walk the chunk list; tolerate alien chunks and short files
	const uint8_t *p   = data + 8 + hlen;
	const uint8_t *end = data + len;
	while (p + 8 <= end && s->ntracks < SMF_MAX_TRACKS && declared) {
		uint32_t id = be32(p), clen = be32(p + 4);
		if (clen > (uint32_t)(end - (p + 8)))
			clen = (uint32_t)(end - (p + 8));    // truncated final chunk
		if (id == 0x4D54726Bu) {                 // "MTrk"
			smf_track_t *t = &s->trk[s->ntracks++];
			t->p = p + 8;
			t->end = p + 8 + clen;
			declared--;
		}
		p += 8 + clen;
	}
	if (s->ntracks == 0)
		return -3;
	smf_rewind(s);
	return 0;
}

void smf_rewind(smf_t *s)
{
	// recompute cursors from the chunk bounds captured at open
	const uint8_t *p   = s->data + 8 + be32(s->data + 4);
	const uint8_t *end = s->data + s->len;
	int n = 0;
	while (p + 8 <= end && n < s->ntracks) {
		uint32_t id = be32(p), clen = be32(p + 4);
		if (clen > (uint32_t)(end - (p + 8)))
			clen = (uint32_t)(end - (p + 8));
		if (id == 0x4D54726Bu) {
			smf_track_t *t = &s->trk[n++];
			t->p = p + 8;
			t->end = p + 8 + clen;
			t->next_tick = 0;
			t->running = 0;
			t->done = 0;
			arm_track(t);              // fetch first delta
		}
		p += 8 + clen;
	}
}

// ---------------------------------------------------------------- iterate

int smf_next(smf_t *s, smf_evt_t *e)
{
	for (;;) {
		// earliest pending track wins (stable: lowest index on ties)
		smf_track_t *t = 0;
		for (int i = 0; i < s->ntracks; i++) {
			smf_track_t *c = &s->trk[i];
			if (!c->done && (!t || c->next_tick < t->next_tick))
				t = c;
		}
		if (!t)
			return 0;                  // all tracks exhausted: song over

		if (t->p >= t->end) {
			t->done = 1;               // ran out without End-of-Track meta
			continue;
		}

		uint32_t tick = t->next_tick;
		uint8_t st = *t->p;
		if (st & 0x80)
			t->p++;
		else if (t->running >= 0x80)
			st = t->running;           // running status reuses last status
		else {
			t->done = 1;               // data byte with no status: corrupt
			continue;
		}

		uint8_t hi = st & 0xF0;
		if (hi >= 0x80 && hi <= 0xE0) {
			t->running = st;
			int n = (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
			if (t->p + n > t->end) {
				t->done = 1;
				continue;
			}
			uint8_t a = *t->p++ & 0x7F;
			uint8_t b = (n == 2) ? (*t->p++ & 0x7F) : 0;
			arm_track(t);
			if (hi == 0xA0 || hi == 0xD0)
				continue;              // aftertouch: nothing to do on OPL
			e->tick = tick;
			e->status = st;
			e->a = a;
			e->b = b;
			e->tempo = 0;
			return 1;
		}

		if (st == 0xF0 || st == 0xF7) {          // sysex: skip payload
			t->running = 0;
			uint32_t sl;
			if (!read_vlq(t, &sl) || sl > (uint32_t)(t->end - t->p)) {
				t->done = 1;
				continue;
			}
			t->p += sl;
			arm_track(t);
			continue;
		}

		if (st == 0xFF) {                        // meta
			t->running = 0;
			if (t->p >= t->end) {
				t->done = 1;
				continue;
			}
			uint8_t type = *t->p++;
			uint32_t ml;
			if (!read_vlq(t, &ml) || ml > (uint32_t)(t->end - t->p)) {
				t->done = 1;
				continue;
			}
			const uint8_t *md = t->p;
			t->p += ml;
			if (type == 0x2F) {                  // End of Track
				t->done = 1;
				continue;
			}
			arm_track(t);
			if (type == 0x51 && ml == 3) {       // Set Tempo (us/quarter)
				uint32_t tempo = ((uint32_t)md[0] << 16)
				               | ((uint32_t)md[1] << 8) | md[2];
				if (tempo == 0)
					continue;                    // nonsense: keep current
				e->tick = tick;
				e->status = 0xFF;
				e->a = e->b = 0;
				e->tempo = tempo;
				return 1;
			}
			continue;                            // other metas: skip
		}

		t->done = 1;                             // 0xF1..0xF6 etc: corrupt
	}
}
