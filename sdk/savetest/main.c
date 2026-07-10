// savetest — deterministic save/exit/re-pick exerciser for the full-system
// simulation (soc/sim). No input, no timing dependence: every step posts a
// code on sys_diag() that the testbench asserts against.
//
// Pass 1 (fresh file):  open (expect created=1), fill pattern, commit, exit.
// Pass 2 (after the TB re-picks us): open (expect 0), verify pattern, open a
// second file (slot rebind), commit it, then re-commit file 1 (rebind-back
// path), exit.
//
// Diag codes: 0xD1A6xxxx, see D() sites. 0xD1A600F0/F1 = pass 1/2 complete.
//
// SPDX-License-Identifier: BSD-2-Clause
#include "hal.h"

#define D(x) sys_diag(0xD1A60000u | (x))

#define WORDS 2048              // 8 KB file
#define MAGIC 0x53494D31u       // "SIM1"

int main(void)
{
	sys_init();
	D(0x001);

	save_file_t f;
	int r = save_open("simtest", WORDS * 4, &f);
	D(0x100 | ((unsigned)(r < 0 ? 0x80 | -r : r) & 0xFF));
	if (r < 0) {
		D(0xBAD);
		sys_exit();
	}
	uint32_t *w = (uint32_t *)f.base;

	if (r == 0 && w[0] == MAGIC) {
		// ---- pass 2: verify yesterday's data survived the SD round-trip
		int ok = 1;
		for (int i = 1; i < WORDS; i++)
			if (w[i] != 0xA5000000u + i) { ok = 0; break; }
		D(ok ? 0x0DD : 0xBAD);

		save_file_t g;                          // second file: rebind path
		int r2 = save_open("simtest2", 256, &g);
		D(0x200 | ((unsigned)(r2 < 0 ? 0x80 | -r2 : r2) & 0xFF));
		if (r2 >= 0) {
			((uint32_t *)g.base)[0] = 0xCAFE0001u;
			D(0x300 | ((unsigned)-save_commit(&g) & 0xFF));
		}
		w[1] ^= 0xFF;                           // dirty file 1, rebind back
		D(0x400 | ((unsigned)-save_commit(&f) & 0xFF));
		D(0x0F1);
	} else {
		// ---- pass 1: brand-new (or garbage) file — write the pattern
		w[0] = MAGIC;
		for (int i = 1; i < WORDS; i++)
			w[i] = 0xA5000000u + i;
		D(0x500 | ((unsigned)-save_commit(&f) & 0xFF));
		D(0x0F0);
	}
	sys_exit();
}
