// fmtest — deterministic FM exerciser for the full-system simulation on the
// opl3 branch. Same register sequence as fmdemo's piano (patch + key-on),
// no input needed. Diag codes tell the TB where we are; the TB itself
// watches audio_mix_l for nonzero samples and reads back opl_dbg.
//
// SPDX-License-Identifier: BSD-2-Clause
#include "hal.h"
#include <generated/csr.h>              // main_opl_dbg (FM flavor diagnostic)

#define D(x) sys_diag(0xF3D00000u | (x))

static const uint8_t op_mod0 = 0x00, op_car0 = 0x03;

int main(void)
{
	sys_init();
	D(0x001);

	const hal_caps_t *caps = sys_caps();
	D(0x100 | (caps->features & 0xFF));     // expect 0x13F on the FM flavor

	// fmdemo's exact init + patch on channel 0
	opl_write(0x105, 0x01);                 // OPL3 mode
	opl_write(0x01,  0x00);
	opl_write(0xBD,  0x00);
	opl_write(0x20 + op_mod0, 0x01);
	opl_write(0x40 + op_mod0, 0x18);
	opl_write(0x60 + op_mod0, 0xF4);
	opl_write(0x80 + op_mod0, 0x47);
	opl_write(0xE0 + op_mod0, 0x00);
	opl_write(0x20 + op_car0, 0x01);
	opl_write(0x40 + op_car0, 0x00);
	opl_write(0x60 + op_car0, 0xF2);
	opl_write(0x80 + op_car0, 0x47);
	opl_write(0xE0 + op_car0, 0x00);
	opl_write(0xC0 + 0, 0x30);              // L+R on, FM alg
	D(0x002);

	// key-on: middle C (fnum 0x157, block 4)
	opl_write(0xA0, 0x57);
	opl_write(0xB0, 0x20 | (4 << 2) | 0x1);
	D(0x003);

	// let the envelope open and samples flow; report the debug word live
	for (int i = 0; i < 200; i++) {
		sys_delay_us(1000);                 // 200 ms total
		D(0x400 | (main_opl_dbg_read() >> 8)); // [15]=nz [14]=valid [13:10]=kon
	}

	// ---- retrigger experiment (field: percussion/fast leads drop notes) ----
	// The patch is percussive (EGT=0): the envelope decays to silence while
	// the key is held. Hypothesis: a keyoff->keyon whose kon 1->0->1 falls
	// inside one ~20 us channel slot is never OBSERVED by the envelope
	// generator (the register file updates, the generator samples too late)
	// -> no re-attack -> dropped note. R1 rewrites kon back-to-back (~4-6 us
	// apart, faster than DOS ISA ever wrote); R2 spaces them 30 us. The TB
	// listens for sound inside each window.
	// Re-patch ch0 as a DRUM (runs 1-2 were inconclusive: the piano patch's
	// decay rate 2 takes seconds to fade — never silent inside the window):
	// AR=F, DR=D, SL=F, RR=F — spike and die in ~50 ms.
	opl_write(0x60 + op_car0, 0xFD);
	opl_write(0x80 + op_car0, 0xFF);
	opl_write(0x60 + op_mod0, 0xFD);
	opl_write(0x80 + op_mod0, 0xFF);
	opl_write(0xB0, (4 << 2) | 0x1);        // keyoff
	sys_delay_us(100);
	opl_write(0xB0, 0x20 | (4 << 2) | 0x1); // clean keyon: audible spike
	D(0x010);
	sys_delay_us(400000);                   // drum dies in ~50 ms; big margin
	D(0x011);                               // silence checkpoint
	D(0x012);
	// R1 x8: statistical — if the envelope generator misses edges that fall
	// inside one channel slot, a fraction of these drums stay silent.
	for (int r = 0; r < 8; r++) {
		opl_write(0xB0, (4 << 2) | 0x1);        // keyoff
		opl_write(0xB0, 0x20 | (4 << 2) | 0x1); // keyon, back-to-back
		sys_delay_us(150000);                   // drum audible ~50 ms, then quiet
	}
	D(0x013);                               // R1 listen-window end
	sys_delay_us(400000);                   // die again (if R1 sounded)
	D(0x014);
	opl_write(0xB0, (4 << 2) | 0x1);        // keyoff
	sys_delay_us(30);                       // authentic-ish ISA-era gap
	opl_write(0xB0, 0x20 | (4 << 2) | 0x1); // keyon, paced (R2)
	D(0x015);
	sys_delay_us(300000);
	D(0x016);                               // R2 listen-window end
	D(0x0F0);                               // done
	for (;;)
		;
}
