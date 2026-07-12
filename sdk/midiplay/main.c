// MIDI PLAYER — a Standard MIDI File jukebox for the RISC-V Stack's REAL
// OPL3 synthesizer. The CPU never touches a sample: it parses the .mid,
// schedules events against sys_ticks_us(), and forwards register writes to
// the FM silicon via opl_write(). See README.md for the architecture.
//
// Controls
//   browser:  UP/DOWN select · A play · SELECT cycle patch bank
//   playing:  A pause/resume · LEFT/RIGHT prev/next song · B back to list
//             SELECT cycle patch bank (re-voices live) · SELECT+START exit
//
// SPDX-License-Identifier: BSD-2-Clause

#include <stdint.h>
#include <string.h>
#include "hal.h"
#include "pakfs.h"
#include "font8x8_basic.h"
#include "smf.h"
#include "bank.h"
#include "oplgm.h"

// ---------------------------------------------------------------- drawing

static int W, H;
static uint8_t *fb;

#define C_BG      0x01                 // near-black blue
#define C_PANEL   0x05                 // dark blue panel
#define C_TEXT    0xFF                 // white
#define C_DIM     0x49                 // grey
#define C_ACCENT  0x1F                 // cyan
#define C_HILITE  0x0F                 // deep cyan row highlight
#define C_BAR     0x1C                 // green (channel activity)
#define C_BARHOT  0xFC                 // yellow (loud)
#define C_DRUM    0xE8                 // orange-red (percussion)
#define C_LED     0x1F                 // voice LED on
#define C_LEDOFF  0x25                 // voice LED off
#define C_WARN    0xE0                 // red

static void rect(int x, int y, int w, int h, uint8_t c)
{
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > W) w = W - x;
	if (y + h > H) h = H - y;
	for (int j = 0; j < h; j++)
		for (int i = 0; i < w; i++)
			fb[(y + j) * W + (x + i)] = c;
}

static void text(const char *s, int x0, int y0, int scale, uint8_t col)
{
	for (int ci = 0; s[ci]; ci++) {
		const char *g = font8x8_basic[(uint8_t)s[ci] & 0x7F];
		for (int ry = 0; ry < 8; ry++)
			for (int rx = 0; rx < 8; rx++)
				if ((g[ry] >> rx) & 1)
					rect(x0 + (ci * 8 + rx) * scale,
					     y0 + ry * scale, scale, scale, col);
	}
}

static void center(const char *s, int y, int scale, uint8_t col)
{
	int len = 0;
	while (s[len]) len++;
	text(s, (W - len * 8 * scale) / 2, y, scale, col);
}

static void mmss(char *dst, uint32_t seconds)
{
	uint32_t m = seconds / 60, s = seconds % 60;
	if (m > 99) m = 99;
	dst[0] = (char)('0' + m / 10); dst[1] = (char)('0' + m % 10);
	dst[2] = ':';
	dst[3] = (char)('0' + s / 10); dst[4] = (char)('0' + s % 10);
	dst[5] = 0;
}

// ---------------------------------------------------------------- catalog

#define MAX_SONGS 64
#define MAX_BANKS 8

static int  song_idx[MAX_SONGS], nsongs;   // pakfs directory indices
static int  bank_idx[MAX_BANKS], nbanks;   // -1 sentinel = built-in
static bank_t cur_bank, fb_bank;           // selected + fallback (drums etc.)
static int  cur_bank_no = -1;              // position in bank_idx[]
static int  fb_bank_ok;

static int has_suffix(const char *n, const char *suf)
{
	int nl = (int)strlen(n), sl = (int)strlen(suf);
	if (nl < sl)
		return 0;
	for (int i = 0; i < sl; i++) {
		char a = n[nl - sl + i], b = suf[i];
		if (a >= 'A' && a <= 'Z') a += 32;
		if (a != b)
			return 0;
	}
	return 1;
}

static int is_bank_name(const char *n)
{
	return !strncmp(n, "banks/", 6)
	    && (has_suffix(n, ".op2") || has_suffix(n, ".ibk")
	     || has_suffix(n, ".tmb") || has_suffix(n, ".opl")
	     || has_suffix(n, ".ad"));
}

static void scan_pak(void)
{
	nsongs = nbanks = 0;
	int n = pakfs_nfiles();
	for (int i = 0; i < n; i++) {
		const char *name = pakfs_name(i);
		if (!name)
			continue;
		if (is_bank_name(name) && nbanks < MAX_BANKS)
			bank_idx[nbanks++] = i;
		else if (has_suffix(name, ".mid") && nsongs < MAX_SONGS)
			song_idx[nsongs++] = i;
	}
}

// short display name: strip directory and extension
static void nice_name(const char *in, char *out, int cap)
{
	const char *base = in;
	for (const char *p = in; *p; p++)
		if (*p == '/')
			base = p + 1;
	int len = (int)strlen(base);
	if (len > 4 && base[len - 4] == '.')
		len -= 4;
	if (len > cap - 1)
		len = cap - 1;
	memcpy(out, base, (size_t)len);
	out[len] = 0;
	for (int i = 0; i < len; i++)      // font is nicer in caps
		if (out[i] >= 'a' && out[i] <= 'z')
			out[i] = (char)(out[i] - 32);
}

// load bank #no (index into bank_idx; -1 = built-in), keep fallback intact
static int select_bank(int no)
{
	if (no < 0 || nbanks == 0) {
		bank_builtin(&cur_bank);
		cur_bank_no = -1;
		oplgm_set_bank(&cur_bank, fb_bank_ok ? &fb_bank : 0);
		return 0;
	}
	no %= nbanks;
	uint32_t sz;
	const void *d = pakfs_data(pakfs_name(bank_idx[no]), &sz);
	if (!d || bank_load(&cur_bank, pakfs_name(bank_idx[no]),
	                    (const uint8_t *)d, sz) < 0)
		return -1;
	cur_bank_no = no;
	oplgm_set_bank(&cur_bank, fb_bank_ok ? &fb_bank : 0);
	return 0;
}

// the fallback bank fills percussion/melodic holes: first loadable .op2
// (op2 banks are the only complete ones), else first loadable anything
static void load_fallback(void)
{
	fb_bank_ok = 0;
	for (int pass = 0; pass < 2 && !fb_bank_ok; pass++)
		for (int i = 0; i < nbanks && !fb_bank_ok; i++) {
			const char *name = pakfs_name(bank_idx[i]);
			if (pass == 0 && !has_suffix(name, ".op2"))
				continue;
			uint32_t sz;
			const void *d = pakfs_data(name, &sz);
			if (d && bank_load(&fb_bank, name, (const uint8_t *)d, sz) == 0)
				fb_bank_ok = 1;
		}
	if (!fb_bank_ok) {
		bank_builtin(&fb_bank);
		fb_bank_ok = 1;
	}
}

// ---------------------------------------------------------------- save

static save_file_t savf;
static int save_ok = -1;
struct saverec { uint32_t magic; char bank[48]; };
#define SAVE_MAGIC 0x4D504C31u         // 'MPL1'

static void save_bank_choice(void)
{
	if (save_ok < 0)
		return;
	struct saverec *r = (struct saverec *)savf.base;
	r->magic = SAVE_MAGIC;
	memset(r->bank, 0, sizeof(r->bank));
	if (cur_bank_no >= 0)
		strncpy(r->bank, pakfs_name(bank_idx[cur_bank_no]),
		        sizeof(r->bank) - 1);
	save_commit(&savf);
}

static void restore_bank_choice(void)
{
	if (save_ok < 0)
		return;
	const struct saverec *r = (const struct saverec *)savf.base;
	if (r->magic != SAVE_MAGIC)
		return;
	for (int i = 0; i < nbanks; i++)
		if (!strncmp(r->bank, pakfs_name(bank_idx[i]), sizeof(r->bank)))
			(void)select_bank(i);
}

// ---------------------------------------------------------------- player

typedef struct {
	smf_t     smf;
	int       loaded, playing, ended;
	uint32_t  tempo;                   // us per quarter note
	uint32_t  last_tick;
	uint64_t  frac;                    // tick->us division remainder
	uint64_t  next_due;                // song-clock us of pending event
	smf_evt_t pend;
	int       has_pend;
	uint64_t  clock;                   // song-clock us (pauses freeze it)
	uint32_t  last_ticks;              // sys_ticks_us at last pump
	uint64_t  total_us;                // from the load-time pre-scan
	uint32_t  nnotes;
	char      title[28];
} player_t;

static player_t pl;
static uint8_t  chan_act[16];          // per-MIDI-channel viz level

static void fetch_next(void)
{
	pl.has_pend = smf_next(&pl.smf, &pl.pend) == 1;
	if (!pl.has_pend)
		return;
	uint32_t dt = pl.pend.tick - pl.last_tick;
	uint64_t num = (uint64_t)dt * pl.tempo + pl.frac;
	pl.next_due += num / pl.smf.division;
	pl.frac      = num % pl.smf.division;
	pl.last_tick = pl.pend.tick;
}

static void player_rewind(void)
{
	smf_rewind(&pl.smf);
	pl.tempo = 500000;                 // SMF default: 120 bpm
	pl.last_tick = 0;
	pl.frac = 0;
	pl.next_due = 0;
	pl.clock = 0;
	pl.ended = 0;
	fetch_next();
}

static void dispatch(const smf_evt_t *e)
{
	int ch = e->status & 0x0F;
	switch (e->status & 0xF0) {
	case 0x80:
		oplgm_note_off(ch, e->a);
		break;
	case 0x90:
		oplgm_note_on(ch, e->a, e->b);
		if (e->b) {
			if (e->b > chan_act[ch])
				chan_act[ch] = e->b;
		}
		break;
	case 0xB0:
		oplgm_control(ch, e->a, e->b);
		break;
	case 0xC0:
		oplgm_program(ch, e->a);
		break;
	case 0xE0:
		oplgm_bend(ch, (e->b << 7) | e->a);
		break;
	case 0xF0:                         // 0xFF: tempo change
		pl.tempo = e->tempo;
		break;
	}
}

// pre-scan: total duration + note count (bounded work, all integer)
static void prescan(void)
{
	smf_evt_t e;
	uint32_t tempo = 500000, last_tick = 0, notes = 0;
	uint64_t us = 0, frac = 0;
	smf_rewind(&pl.smf);
	for (uint32_t guard = 0; guard < 4000000 && smf_next(&pl.smf, &e); guard++) {
		uint64_t num = (uint64_t)(e.tick - last_tick) * tempo + frac;
		us  += num / pl.smf.division;
		frac = num % pl.smf.division;
		last_tick = e.tick;
		if (e.status == 0xFF)
			tempo = e.tempo;
		else if ((e.status & 0xF0) == 0x90 && e.b)
			notes++;
	}
	pl.total_us = us;
	pl.nnotes = notes;
}

static int load_song(int i)
{
	oplgm_all_off();
	memset(&pl, 0, sizeof(pl));
	memset(chan_act, 0, sizeof(chan_act));
	pl.last_ticks = sys_ticks_us();    // else the first pump step spans the
	                                   // whole uptime and rushes early notes
	if (i < 0 || i >= nsongs)
		return -1;
	uint32_t sz;
	const void *d = pakfs_data(pakfs_name(song_idx[i]), &sz);
	if (!d || smf_open(&pl.smf, (const uint8_t *)d, sz) < 0)
		return -1;
	nice_name(pakfs_name(song_idx[i]), pl.title, sizeof(pl.title));
	prescan();
	player_rewind();
	pl.loaded = 1;
	return 0;
}

static void player_pump(void)
{
	uint32_t now = sys_ticks_us();
	uint32_t step = now - pl.last_ticks;       // unsigned wrap-safe
	pl.last_ticks = now;
	if (!pl.loaded || !pl.playing || pl.ended)
		return;
	pl.clock += step;
	while (pl.has_pend && pl.next_due <= pl.clock)
	{
		dispatch(&pl.pend);
		fetch_next();
	}
	if (!pl.has_pend) {
		oplgm_all_off();               // song done: leave nothing ringing
		pl.playing = 0;
		pl.ended = 1;
	}
}

// ---------------------------------------------------------------- UI

enum { SCR_BROWSER, SCR_PLAYING, SCR_NOFM };

static int cur_song, sel;

static void draw_bankline(int y)
{
	char bn[26];
	nice_name(cur_bank.name, bn, sizeof(bn));
	text("BANK", 12, y, 1, C_DIM);
	text(bn, 52, y, 1, C_ACCENT);
	if (save_ok == 0 || save_ok == 1)
		text("SAVED", W - 52, y, 1, C_DIM);
}

static void draw_browser(uint32_t frame)
{
	rect(0, 0, W, H, C_BG);
	rect(0, 0, W, 26, C_PANEL);
	text("MIDI PLAYER", 12, 6, 2, C_TEXT);
	text("OPL3", W - 76, 6, 2, C_ACCENT);

	if (nsongs == 0) {
		center("NO .MID FILES IN PAK", 100, 1, C_WARN);
		center("PACK SONGS WITH MAKE PAK", 116, 1, C_DIM);
	}
	int page = 10, top = sel - sel % page;
	for (int r = 0; r < page && top + r < nsongs; r++) {
		int i = top + r, y = 36 + r * 16;
		char name[32];
		nice_name(pakfs_name(song_idx[i]), name, sizeof(name));
		if (i == sel) {
			rect(6, y - 3, W - 12, 14, C_HILITE);
			text(">", 10, y, 1, C_ACCENT);
		}
		text(name, 24, y, 1, i == sel ? C_TEXT : C_DIM);
	}
	if (nsongs > page) {
		char pg[16];                   // "PAGE x/y"
		int cur = sel / page + 1, tot = (nsongs + page - 1) / page;
		pg[0]='P';pg[1]='G';pg[2]=' ';pg[3]=(char)('0'+cur);
		pg[4]='/';pg[5]=(char)('0'+tot);pg[6]=0;
		text(pg, W - 60, 30, 1, C_DIM);
	}
	draw_bankline(H - 36);
	if (frame & 32)
		center("A:PLAY  SELECT:BANK  SEL+START:EXIT", H - 20, 1, C_DIM);
}

static void draw_playing(uint32_t frame)
{
	rect(0, 0, W, H, C_BG);
	rect(0, 0, W, 26, C_PANEL);
	text(pl.title, 12, 6, 2, C_TEXT);

	// time + progress
	char t[8];
	mmss(t, (uint32_t)(pl.clock / 1000000u));
	text(t, 12, 34, 1, C_TEXT);
	mmss(t, (uint32_t)(pl.total_us / 1000000u));
	text(t, W - 52, 34, 1, C_DIM);
	int px0 = 60, pw = W - 120;
	rect(px0, 36, pw, 4, C_PANEL);
	if (pl.total_us)
		rect(px0, 36, (int)((uint64_t)pw * pl.clock / pl.total_us), 4,
		     C_ACCENT);

	const char *st = pl.ended ? "DONE " : pl.playing ? "PLAY " : "PAUSE";
	text(st, 12, 48, 1, pl.playing ? C_ACCENT : C_WARN);
	char ns[12];                       // "TRK i/N"
	int cur = cur_song + 1;
	ns[0]='T';ns[1]='R';ns[2]='K';ns[3]=' ';
	int k = 4;
	if (cur >= 10) ns[k++] = (char)('0' + cur / 10);
	ns[k++] = (char)('0' + cur % 10);
	ns[k++] = '/';
	if (nsongs >= 10) ns[k++] = (char)('0' + nsongs / 10);
	ns[k++] = (char)('0' + nsongs % 10);
	ns[k] = 0;
	text(ns, W - 76, 48, 1, C_DIM);

	// 16 channel activity bars (drums in orange), note dots above
	int bx = 12, bw = 14, gap = 5, base = 176, maxh = 96;
	for (int c = 0; c < 16; c++) {
		int x = bx + c * (bw + gap);
		rect(x, base - maxh, bw, maxh, C_PANEL);
		int h = (chan_act[c] * maxh) / 127;
		if (h > 0)
			rect(x, base - h, bw,  h,
			     c == 9 ? C_DRUM : (h > maxh * 3 / 4 ? C_BARHOT : C_BAR));
		char d[3];
		int cn = c + 1;
		d[0] = cn >= 10 ? '1' : (char)('0' + cn);
		d[1] = cn >= 10 ? (char)('0' + cn - 10) : 0;
		d[2] = 0;
		text(d, x + (bw - (cn >= 10 ? 16 : 8)) / 2, base + 4, 1, C_DIM);
	}

	// 18 hardware-voice LEDs, note position as a dot column
	int ly = 196;
	text("FM", 12, ly + 1, 1, C_DIM);
	for (int v = 0; v < OPLGM_NVOICE; v++) {
		int mc, note = oplgm_voice_note(v, &mc);
		int x = 34 + v * 15;
		rect(x, ly, 11, 10, note >= 0 ? (mc == 9 ? C_DRUM : C_LED)
		                              : C_LEDOFF);
		if (note >= 0)
			rect(x + 2, ly + 8 - (note * 8) / 127 - 1, 7, 2, C_BG);
	}

	draw_bankline(H - 26);
	if (frame & 32)
		center("A:PAUSE  L/R:SONG  B:LIST  SELECT:BANK", H - 12, 1, C_DIM);
	(void)frame;
}

static void draw_nofm(void)
{
	rect(0, 0, W, H, C_BG);
	center("MIDI PLAYER", 60, 3, C_TEXT);
	rect(60, 100, W - 120, 2, C_WARN);
	center("FM CORE REQUIRED", 116, 2, C_WARN);
	center("THIS APP PLAYS ON THE OPL3 SYNTHESIZER", 144, 1, C_DIM);
	center("OF THE RISCVSTACK FM FLAVOR.", 156, 1, C_DIM);
	center("PICK THE FM CORE AND RELOAD.", 176, 1, C_TEXT);
	center("SELECT+START: EXIT TO PICKER", H - 24, 1, C_DIM);
}

// ---------------------------------------------------------------- main

int main(void)
{
	sys_init();
	W = fb_width();
	H = fb_height();

	int have_fm = (sys_caps()->features & HAL_FEAT_FM) != 0;
	int screen = have_fm ? SCR_BROWSER : SCR_NOFM;
	int toast = 0;                     // frames left of "BAD FILE" warning

	if (have_fm) {
		oplgm_init();
		if (pakfs_mount() == 0)
			scan_pak();
		load_fallback();
		(void)select_bank(nbanks ? 0 : -1);
		save_ok = save_open("midiplay", sizeof(struct saverec), &savf);
		restore_bank_choice();
	}

	uint32_t prev = 0, frame = 0;
	pl.last_ticks = sys_ticks_us();

	for (;;) {
		fb = fb_backbuffer();
		input_poll();
		uint32_t btn  = input_buttons(0);
		uint32_t edge = btn & ~prev;
		prev = btn;

		if ((btn & HAL_BTN_SELECT) && (btn & HAL_BTN_START))
			sys_exit();

		player_pump();

		// auto-advance the jukebox when a song finishes
		if (screen == SCR_PLAYING && pl.ended && nsongs > 0) {
			cur_song = (cur_song + 1) % nsongs;
			sel = cur_song;
			if (load_song(cur_song) == 0)
				pl.playing = 1;
			else
				screen = SCR_BROWSER;
		}

		if ((edge & HAL_BTN_SELECT) && have_fm && nbanks > 0) {
			int next = (cur_bank_no + 1) % nbanks;
			if (select_bank(next) == 0)
				save_bank_choice();     // bad bank file: keep current
		}

		switch (screen) {
		case SCR_BROWSER:
			if ((edge & HAL_BTN_UP) && nsongs)
				sel = (sel + nsongs - 1) % nsongs;
			if ((edge & HAL_BTN_DOWN) && nsongs)
				sel = (sel + 1) % nsongs;
			if ((edge & HAL_BTN_A) && nsongs) {
				cur_song = sel;
				if (load_song(cur_song) == 0) {
					pl.playing = 1;
					screen = SCR_PLAYING;
				} else {
					toast = 120;        // malformed: shrug, stay here
				}
			}
			draw_browser(frame);
			if (toast > 0) {
				toast--;
				rect(40, 104, W - 80, 24, C_PANEL);
				center("BAD/UNSUPPORTED MIDI FILE - SKIPPED", 112, 1,
				       C_WARN);
			}
			break;

		case SCR_PLAYING: {
			int hop = 0;
			if (edge & HAL_BTN_A) {
				if (pl.ended) {         // replay from the top
					player_rewind();
					pl.playing = 1;
				} else {
					pl.playing = !pl.playing;
					if (!pl.playing)
						oplgm_all_off();
				}
			}
			if (edge & HAL_BTN_RIGHT)
				hop = 1;
			if (edge & HAL_BTN_LEFT)
				hop = -1;
			if (hop && nsongs) {
				cur_song = (cur_song + nsongs + hop) % nsongs;
				sel = cur_song;
				if (load_song(cur_song) == 0)
					pl.playing = 1;
				else
					screen = SCR_BROWSER;
			}
			if (edge & HAL_BTN_B) {
				oplgm_all_off();
				pl.playing = 0;
				screen = SCR_BROWSER;
			}
			draw_playing(frame);
			break;
		}

		default:
			draw_nofm();
			break;
		}

		// decay the channel bars ~1 s full-scale on WALL TIME — the frame
		// rate is vsync on hardware but unthrottled under SDL's dummy
		// driver, so visuals must never be tied to the frame count
		{
			static uint32_t decay_prev, decay_acc;
			uint32_t dnow = sys_ticks_us();
			decay_acc += dnow - decay_prev;
			decay_prev = dnow;
			if (decay_acc > 250000)
				decay_acc = 250000;    // first frame / stall clamp
			for (; decay_acc >= 8000; decay_acc -= 8000)
				for (int c = 0; c < 16; c++)
					if (chan_act[c])
						chan_act[c] -= (chan_act[c] >= 2) ? 2 : chan_act[c];
		}

		fb_present();
		frame++;
	}
	return 0;
}
