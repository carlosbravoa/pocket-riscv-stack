// vmxtest — full-system exerciser for the 32-voice hardware sample mixer
// (soc/voice_mixer/, "vmx" CSR region). Runs in the Verilator sim via
//   GAME=vmxtest ./run_sim.sh
// and reports through the TB's generic portlib diag protocol:
//   0x9AC0xxxx progress, 0x9AC00BAD = step failed, 0x9AC000F0 = all passed.
//
// What it proves IN-SYSTEM (the P1 bench already proved the math bit-exact):
//   - CSR programming + atomic key-on reach the core,
//   - the per-voice line cache + LiteDRAMDMAReader actually fetch from DRAM
//     (positions only advance when fetches complete — a wedged DMA freezes
//     them, which this test catches),
//   - frame pacing (~48 kHz) drives advancement at the right RATE,
//   - forward loops stay bounded, one-shots self-retire.
//
// NOTE (layering): this TEST pokes generated CSR accessors directly. Games
// must use the HAL vmx_* API once it lands (P3) — do not copy this pattern.
//
// SPDX-License-Identifier: BSD-2-Clause
#include "hal.h"
#include <string.h>
#include <system.h>
#include <generated/csr.h>

#define D(x) sys_diag(0x9AC00000u | (x))
#define FAIL() do { sys_diag(0x9AC00BADu); for(;;) ; } while (0)

#define MAIN_RAM 0x40000000u

// vmx field helpers (mirror the CSR layout)
static void key_on(int v, uint32_t base, uint32_t len, int fmt, int loop,
                   uint32_t ls, uint32_t le, uint32_t step, int vol, int pan)
{
	vmx_sel_write(v);
	vmx_smp_base_write(base);
	vmx_smp_len_write(len);
	vmx_smp_fmt_write((loop << 2) | fmt);
	vmx_loop_start_write(ls);
	vmx_loop_end_write(le);
	vmx_step_write(step);
	vmx_volpan_write((vol << 8) | pan);
	vmx_ctrl_write(1);              // commit + key_on
}

static uint32_t pos_of(int v) { vmx_sel_write(v); return vmx_pos_read(); }

static int16_t samples[1024];       // lives in main_ram (game runs from DRAM)

int main(void)
{
	sys_init();
	D(0x001);

	// test sample: s16 ramp
	for (int i = 0; i < 1024; i++)
		samples[i] = (int16_t)(i * 13 - 6000);
	flush_cpu_dcache_range(samples, sizeof(samples));
	uint32_t base = (uint32_t)(uintptr_t)samples - MAIN_RAM;  // byte offset in main_ram
	D(0x002);

	vmx_master_write(0xFF);

	// v0: one-shot, 64 frames at 0.25x -> retires after 256 output frames (~5.3 ms)
	key_on(0, base, 64, 2, 0, 0, 0, 0x4000, 255, 128);
	// v1: forward loop over 256 frames at 1.0x
	key_on(1, base, 1024, 2, 1, 0, 256, 0x10000, 200, 64);
	// v2: forward inner loop [100,400) at 2.0x
	key_on(2, base, 1024, 2, 1, 100, 400, 0x20000, 150, 200);
	D(0x003);

	// (a) all three active?
	uint32_t am = vmx_active_read();
	if ((am & 0x7) != 0x7) FAIL();
	D(0x004);

	// (b) advancement RATE: v1 at 1.0x must advance ~48 frames in 1 ms
	uint32_t p0 = pos_of(1);
	sys_delay_us(1000);
	uint32_t p1 = pos_of(1);
	uint32_t adv = (p1 - p0) & 0xFFFFFF;        // (loop span 256 > 48: no wrap ambiguity
	                                            //  as long as we sample within one lap)
	if (adv < 40 || adv > 56) FAIL();           // 48 +/- jitter/CSR-latency slack
	D(0x005);

	// (c) loop bound: v2's position must stay inside [ls, le)
	for (int i = 0; i < 20; i++) {
		uint32_t p = pos_of(2);
		if (p >= 400) FAIL();
		sys_delay_us(200);
	}
	D(0x006);

	// (d) one-shot retirement: v0 ends by ~5.3 ms; give it 8 ms total
	sys_delay_us(8000);
	am = vmx_active_read();
	if (am & 0x1) FAIL();                       // v0 must be done
	if ((am & 0x6) != 0x6) FAIL();              // v1/v2 still looping
	D(0x007);

	// (e) key_off is immediate
	vmx_sel_write(1);
	vmx_ctrl_write(2);
	am = vmx_active_read();
	if (am & 0x2) FAIL();
	D(0x008);

	// (f) live volume/master writes don't disturb the engine
	vmx_sel_write(2);
	vmx_volpan_write((80 << 8) | 128);
	vmx_master_write(0x80);
	sys_delay_us(500);
	if (!(vmx_active_read() & 0x4)) FAIL();
	D(0x009);

	D(0x0F0);                                   // portlib "all passed"
	for (;;)
		;
}
