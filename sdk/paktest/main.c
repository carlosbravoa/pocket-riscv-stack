// paktest — verifies auto-load-by-name: pak_bind_named() + pak_slot_read() with
// header-derived sizing, WITHOUT the APF data-table slot size (the production
// "user didn't pick the Pak slot" condition, modelled by the TB's --autopak).
//
// The pak is a synthetic pakfs whose every file is filled with the 0xC3 marker,
// so a correct read proves the openfile-bound slot delivers real bytes (not
// zeros/garbage) even though main_pak_size is 0.
//
// Diag codes: 0x0FACxxxx.  0x0FAC00F0 = PASS. 0x0FAC00Bn / 0Cn / 0Dn = failures.
//
// SPDX-License-Identifier: BSD-2-Clause
#include "hal.h"

#define D(x) sys_diag(0x0FAC0000u | (x))
#define PAKFS_MAGIC 0x464B4150u          // "PAKF"
#define OFF 0x02100000u                  // landing zone above the game region

typedef struct { char name[48]; uint32_t offset; uint32_t size; } pentry; // 56 B

int main(void)
{
	sys_init();
	D(0x001);

	// 1. bind the file by name (openfile) — no manual pick, no datatable size
	if (pak_bind_named("paktest.pak") != 0) { D(0x0B1); for (;;) ; }

	const uint8_t  *base = (const uint8_t *)(0x40000000u + OFF);
	const uint32_t *h    = (const uint32_t *)base;

	// 2. read the 16-byte header off the bound slot
	if (pak_slot_read(OFF, 0, 16) != 0)          { D(0x0B2); for (;;) ; }
	if (h[0] != PAKFS_MAGIC || h[1] != 1)        { D(0x0B3); for (;;) ; }
	uint32_t n = h[2];
	D(0x200u | (n & 0xFF));                       // report entry count

	// 3. read the directory, compute the logical total
	uint32_t dir = 16 + n * (uint32_t)sizeof(pentry);
	if (pak_slot_read(OFF, 0, dir) != 0)         { D(0x0B4); for (;;) ; }
	const pentry *e = (const pentry *)(base + 16);
	uint32_t top = dir;
	for (uint32_t i = 0; i < n; i++)
		if (e[i].offset + e[i].size > top)
			top = e[i].offset + e[i].size;

	// 4. pull the whole logical content and verify every file's bytes
	if (pak_slot_read(OFF, 0, top) != 0)         { D(0x0B5); for (;;) ; }
	for (uint32_t i = 0; i < n; i++) {
		uint32_t o = e[i].offset, s = e[i].size;
		if (s == 0 || o + s > top)               { D(0x0C0u | i); for (;;) ; }
		if (base[o] != 0xC3 || base[o + s / 2] != 0xC3 || base[o + s - 1] != 0xC3)
		                                          { D(0x0D0u | i); for (;;) ; }
	}

	D(0x0F0);                                     // PASS
	for (;;) ;                                     // hold the diag latched
}
