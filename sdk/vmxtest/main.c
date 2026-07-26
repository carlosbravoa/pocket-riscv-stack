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
// Uses the HAL vmx_* API (P3) end to end: game -> HAL -> CSRs -> core -> DMA.
//
// SPDX-License-Identifier: BSD-2-Clause
#include "hal.h"
#include <string.h>

#define D(x) sys_diag(0x9AC00000u | (x))
#define FAIL() do { sys_diag(0x9AC00BADu); for(;;) ; } while (0)

static int16_t samples[1024];       // lives in main_ram (game runs from DRAM)

int main(void)
{
	sys_init();
	D(0x001);
	if (!(sys_caps()->features & HAL_FEAT_VOICES) || vmx_voices() != 32)
		FAIL();

	// test sample: s16 ramp
	for (int i = 0; i < 1024; i++)
		samples[i] = (int16_t)(i * 13 - 6000);
	vmx_sample_t one_shot = { .data = samples, .frames = 64,
	                          .format = VMX_FMT_S16, .loop = VMX_LOOP_NONE };
	vmx_sample_t full_loop = { .data = samples, .frames = 1024,
	                           .format = VMX_FMT_S16, .loop = VMX_LOOP_FWD,
	                           .loop_start = 0, .loop_end = 256 };
	vmx_sample_t inner_loop = { .data = samples, .frames = 1024,
	                            .format = VMX_FMT_S16, .loop = VMX_LOOP_FWD,
	                            .loop_start = 100, .loop_end = 400 };
	vmx_sample_flush(&full_loop);       // whole buffer once
	D(0x002);

	vmx_master(0xFF);

	// v0: one-shot, 64 frames at 0.25x -> retires after 256 output frames (~5.3 ms)
	if (vmx_key_on(0, &one_shot, 0x4000, 255, 128) != 0) FAIL();
	// v1: forward loop over 256 frames at 1.0x
	if (vmx_key_on(1, &full_loop, 0x10000, 200, 64) != 1) FAIL();
	// v2: forward inner loop [100,400) at 2.0x
	if (vmx_key_on(2, &inner_loop, 0x20000, 150, 200) != 2) FAIL();
	D(0x003);

	// (a) all three active?
	uint32_t am = vmx_active_mask();
	if ((am & 0x7) != 0x7) FAIL();
	D(0x004);

	// (b) advancement RATE: v1 at 1.0x must advance ~48 frames in 1 ms
	uint32_t p0 = vmx_pos(1);
	sys_delay_us(1000);
	uint32_t p1 = vmx_pos(1);
	uint32_t adv = (p1 - p0) & 0xFFFFFF;        // (loop span 256 > 48: no wrap ambiguity
	                                            //  as long as we sample within one lap)
	if (adv < 40 || adv > 56) FAIL();           // 48 +/- jitter/CSR-latency slack
	D(0x005);

	// (c) loop bound: v2's position must stay inside [ls, le)
	for (int i = 0; i < 20; i++) {
		uint32_t p = vmx_pos(2);
		if (p >= 400) FAIL();
		sys_delay_us(200);
	}
	D(0x006);

	// (d) one-shot retirement: v0 ends by ~5.3 ms; give it 8 ms total
	sys_delay_us(8000);
	am = vmx_active_mask();
	if (am & 0x1) FAIL();                       // v0 must be done
	if ((am & 0x6) != 0x6) FAIL();              // v1/v2 still looping
	D(0x007);

	// (e) key_off is immediate
	vmx_key_off(1, 0);
	am = vmx_active_mask();
	if (am & 0x2) FAIL();
	D(0x008);

	// (f) live volume/master writes + auto-allocation
	vmx_set(2, 0x20000, 80, 128);
	vmx_master(0x80);
	sys_delay_us(500);
	if (!(vmx_active_mask() & 0x4)) FAIL();
	// auto-alloc must hand out a free voice (not 0..2? 0/1 are free now: allocator
	// returns the lowest free) and pcm_play must route to a reserved voice 28..31
	int av = vmx_key_on(-1, &one_shot, 0x10000, 255, 128);
	if (av < 0 || av >= 28) FAIL();
	if (pcm_play(-1, samples, 64, 48000) < 0) FAIL();
	if (!(vmx_active_mask() & 0xF0000000u)) FAIL();
	D(0x009);

	D(0x0F0);                                   // portlib "all passed"
	for (;;)
		;
}
