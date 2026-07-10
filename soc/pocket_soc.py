#!/usr/bin/env python3
#
# Stage-1 SoC for the "RISC-V stack" on the Analogue Pocket.
#
# One SoC definition, two build targets that share an IDENTICAL memory map so a
# single firmware binary runs on both:
#
#   --sim     Verilator simulation (SimPlatform + serial2console). Proves the CPU
#             boots our firmware and talks over UART, with no hardware.
#   --build   Quartus synthesis for the Pocket's Cyclone V 5CEBA4F23C8. Produces a
#             bitstream and, crucially, a timing/fit report ("a bitstream that builds").
#
# Memory map (see ROM_SIZE/SRAM_SIZE below — this comment mirrors those constants):
#   0x0000_0000  ROM   32 KB   firmware (.text/.rodata/.data-init), CPU reset vector
#   0x1000_0000  SRAM  16 KB   .data/.bss/stack
#   0x4000_0000  main_ram 64MB external SDRAM (framebuffer pages live here)
#   0xf000_0000  CSR           uart, timer, video, ctrl
#
# SPDX-License-Identifier: BSD-2-Clause

import argparse

from migen import Signal, ClockDomain, Memory, If, Cat, Mux

from migen.genlib.io       import CRG
from migen.genlib.cdc      import MultiReg
from litex.soc.interconnect.csr import CSRStorage, CSRStatus

from litex.gen import LiteXModule

from litex.soc.integration.soc_core import SoCCore
from litex.soc.integration.builder  import Builder
from litex.soc.cores.clock          import CycloneVPLL
from litex.soc.cores.gpio           import GPIOOut
from litex.soc.integration.soc      import SoCRegion
from litex.soc.interconnect         import wishbone

# Shared memory-map constants (keep sim == hw so one firmware.bin runs on both).
ROM_SIZE   = 0x8000      # 32 KB (VexiiRiscv BIOS is ~+0.6KB over VexRiscv's; framebuffers
                         # now live in DRAM so the BRAM headroom is available)
SRAM_SIZE  = 0x4000      # 16 KB
CPU_TYPE   = "vexiiriscv"   # rv32im (matches the classic-VexRiscv firmware ABI)
CPU_VAR    = "standard"

# ONE sys_clk constant: used by the CLI default, the __init__ default AND the SDRAM
# timing computation (they silently disagreed before: 50 MHz signature default with
# 25 MHz-computed refresh timings = DRAM corruption for direct instantiation).
SYS_CLK_FREQ = int(25e6)


def _configure_vexiiriscv():
    """VexiiRiscv's config comes from an argparse namespace (it's built for LiteX's
    CLI generators). Replicate that programmatically before the CPU is instantiated:
    fill a parser with its args, parse defaults, and run args_read (which sbt-generates
    the ISA metadata; the netlist itself is generated + cached at build time)."""
    import argparse
    from litex.soc.cores.cpu.vexiiriscv.core import VexiiRiscv
    p = argparse.ArgumentParser()
    VexiiRiscv.args_fill(p)
    vargs = p.parse_args([])
    vargs.cpu_variant = CPU_VAR
    # Zicbom (cache-management ops): VexiiRiscv has a write-back L1 D-cache that is NOT
    # coherent with the video DMA, and its generic flush_cpu_dcache() is a no-op. Enabling
    # zicbom adds the CBO hardware (cbo.flush) + defines __riscv_zicbom__ so the HAL can
    # write drawn framebuffer lines back to DRAM before the scanout reads them (fixes the
    # box flicker). With no L2, cbo.flush writes L1 straight to DRAM.
    vargs.isa = ["zicbom"]
    # The VexiiRiscv checkout is pinned; "recommended" re-runs git on the 366 MB tree
    # at EVERY elaboration (breaks offline builds, can reset local experiments).
    vargs.update_repo = "no"
    VexiiRiscv.args_read(vargs)

# External SDRAM (Stage 4): the Pocket's 512Mbit/16-bit SDR chip == AS4C32M16
# (4 banks, 8192 rows, 1024 cols). LiteDRAM GENSDRPHY on hardware; PHY model in sim.
SDRAM_MODULE   = "AS4C32M16"
# Phase (deg) of dram_clk relative to sys_clk. 180 lets the SoC PLL lock cleanly
# (CPU runs); 270 produced a bad LiteX phase calc (~360deg) that broke the PLL ->
# black screen. Keep 180 until the SDRAM is actually initialized (sdram_init), then
# tune read-capture if needed.
DRAM_CLK_PHASE = 180

# Framebuffer: 320x240, 8bpp RGB332, in DRAM (LiteX VideoFramebuffer).
# SINGLE SOURCE of the frame geometry: LiteX derives the DMA length by PARSING the
# timing NAME string ("WxH@..."), not from the timings dict — so the name is built
# from FB_W/FB_H and the dict is asserted against them below. Firmware gets the
# geometry via the generated VIDEO_FRAMEBUFFER_HRES/VRES constants (don't copy it).
FB_W, FB_H, FB_BPP = 320, 240, 8
TIMINGS_NAME = f"{FB_W}x{FB_H}@60Hz"

# Custom video timing matching the Analogue APF raster core_top uses: 400x512 total
# @ ~12.288 MHz => 60 Hz, 320x240 active. Fed to LiteX's VideoTimingGenerator.
APF_TIMINGS = {
    "pix_clk"       : 12.288e6,
    "h_active"      : 320,
    "h_blanking"    : 80,     # 400 total
    "h_sync_offset" : 8,
    "h_sync_width"  : 32,
    "v_active"      : 240,
    "v_blanking"    : 272,    # 512 total
    "v_sync_offset" : 1,
    "v_sync_width"  : 8,
}
assert APF_TIMINGS["h_active"] == FB_W and APF_TIMINGS["v_active"] == FB_H, \
    "APF_TIMINGS active area must match FB_W/FB_H (the DMA length comes from the name string)"


# -----------------------------------------------------------------------------
# Clock/reset generators
# -----------------------------------------------------------------------------

class _CRGHW(LiteXModule):
    """Hardware CRG: PLL clk_74a (74.25 MHz) -> sys_clk. Holds the SoC in reset
    until the PLL locks (otherwise the CPU never gets a clean reset pulse)."""
    def __init__(self, platform, sys_clk_freq):
        self.rst    = Signal()
        self.cd_sys = ClockDomain()

        self.cd_sys_ps = ClockDomain()   # phase-shifted clock -> dram_clk pin

        clk74a = platform.request("clk74a")
        self.pll = pll = CycloneVPLL(speedgrade="-C8")  # 5CEBA4F23C8: commercial, grade 8
        # External reset (APF reset_n from core_top, inverted): resetting the PLL drops
        # `locked`, which the create_clkout AsyncResetSynchronizers already turn into a
        # clean chip-wide reset — so the host/menu reset now truly resets CPU + DMA.
        self.comb += pll.reset.eq(self.rst | platform.request("rst"))
        pll.register_clkin(clk74a, 74.25e6)
        # create_clkout already ties cd_sys.rst to ~pll.locked via an
        # AsyncResetSynchronizer, so the SoC is held in reset until the PLL locks.
        pll.create_clkout(self.cd_sys,    sys_clk_freq)
        pll.create_clkout(self.cd_sys_ps, sys_clk_freq, phase=DRAM_CLK_PHASE)
        # Forward the phase-shifted clock to the SDRAM chip.
        self.comb += platform.request("dram_clk").eq(self.cd_sys_ps.clk)


# -----------------------------------------------------------------------------
# SoC
# -----------------------------------------------------------------------------

class PocketSoC(SoCCore):
    def __init__(self, sim=False, sys_clk_freq=SYS_CLK_FREQ, firmware=None,
                 with_sdram=True, **kwargs):
        if sim:
            from litex.build.sim              import SimPlatform
            from litex.build.generic_platform import Pins, Subsignal
            sim_io = [
                ("sys_clk", 0, Pins("X")),
                ("sys_rst", 0, Pins("X")),
                ("serial",  0,
                    # NOTE: Pins("X"*8) == Pins("XXXXXXXX") is ONE pin (migen splits
                    # on whitespace). The data fields MUST be 8-bit -> use Pins(8),
                    # else the UART byte is truncated to 1 bit and serial2console
                    # reads garbage.
                    Subsignal("source_valid", Pins(1)),
                    Subsignal("source_ready", Pins(1)),
                    Subsignal("source_data",  Pins(8)),
                    Subsignal("sink_valid",    Pins(1)),
                    Subsignal("sink_ready",    Pins(1)),
                    Subsignal("sink_data",     Pins(8)),
                ),
                ("diag", 0, Pins(32)),
                ("vclk", 0, Pins(1)),
                ("video", 0,
                    Subsignal("de",    Pins(1)),
                    Subsignal("hsync", Pins(1)),
                    Subsignal("vsync", Pins(1)),
                    Subsignal("r",     Pins(8)),
                    Subsignal("g",     Pins(8)),
                    Subsignal("b",     Pins(8)),
                ),
            ]
            platform     = SimPlatform("SIM", sim_io)
            sys_clk_freq = int(1e6)  # fast, deterministic sim
            # migen CRG provides the vendor-agnostic power-on reset the CPU needs.
            self.crg     = CRG(platform.request("sys_clk"))
            uart_name    = "sim"
        else:
            import pocket_platform
            platform  = pocket_platform.Platform()
            self.crg  = _CRGHW(platform, sys_clk_freq)
            uart_name = "serial"

        # VexiiRiscv needs its config (ISA/xlen) set from an argparse namespace before
        # SoCCore instantiates it (classic VexRiscv ships pre-generated, so needs none).
        if CPU_TYPE == "vexiiriscv":
            _configure_vexiiriscv()

        SoCCore.__init__(self, platform, clk_freq=sys_clk_freq,
            ident         = "RISC-V stack / Pocket Stage-1",
            ident_version = True,
            cpu_type          = CPU_TYPE,
            cpu_variant       = CPU_VAR,
            integrated_rom_size  = ROM_SIZE,
            integrated_sram_size = SRAM_SIZE,
            integrated_rom_init  = firmware if firmware else [],
            uart_name    = uart_name,
            uart_baudrate= 115200,
            **kwargs)

        # Diagnostic output register: firmware writes a status word (magic + a live
        # counter); Stage 2's core_top renders it on the Pocket LCD (the no-JTAG
        # "hello"). Auto-exposes the `diag_out` CSR -> diag_out_write() in firmware.
        self.diag = GPIOOut(platform.request("diag"))

        # External SDRAM (Stage 4): LiteDRAM -> main_ram at 0x40000000.
        if with_sdram:
            from litedram.modules import AS4C32M16
            # Timings MUST be computed for the clock the controller actually runs at
            # (ns -> cycle counts happen here): hardware uses sys_clk_freq; sim keeps
            # the real hardware clock since the 1 MHz sim clock would make the refresh
            # ratio invalid. 25 MHz gives huge SDR read-capture margin.
            sdram_module = AS4C32M16(SYS_CLK_FREQ if sim else sys_clk_freq, "1:1")
            if sim:
                from litedram.phy.model import SDRAMPHYModel
                self.sdrphy = SDRAMPHYModel(module=sdram_module,
                                            data_width=32, clk_freq=sys_clk_freq)
            else:
                from litedram.phy import GENSDRPHY
                self.sdrphy = GENSDRPHY(platform.request("sdram"), sys_clk_freq)
            # No L2 cache: the 5CEBA4 is small and L2 is the biggest logic/BRAM
            # consumer. Slower DRAM but the design fits (Stage 3 was already 88% BRAM).
            self.add_sdram("sdram", phy=self.sdrphy, module=sdram_module,
                           l2_cache_size=0)

            # Video framebuffer in DRAM (Stage 4 proper): LiteX VideoFramebuffer DMAs
            # the front buffer from main_ram through a scanline FIFO into the pixel
            # clock domain -> RGB stream. Frees all framebuffer BRAM; the front-buffer
            # base is a runtime CSR (video_framebuffer_dma_base) == our page flip.
            from litex.soc.interconnect import stream
            from litex.soc.cores.video import video_data_layout
            self.cd_vid = ClockDomain()
            self.comb += self.cd_vid.clk.eq(platform.request("vclk"))

            # A raw stream endpoint stands in for the PHY: VideoGenericPHY would place
            # the outputs in I/O DDIO cells, but our video pins are module ports (the
            # real pads are up in apf_top), so we register them as plain logic instead.
            vout = stream.Endpoint(video_data_layout)
            self.comb += vout.ready.eq(1)
            # fifo_depth: LiteX's default is 64 KB (85% of a frame!) which costs ~22% of
            # the 5CEBA4's total BRAM. 8 KB still buffers ~650 us of pixels against a
            # DRAM port with ~8x bandwidth headroom — and a small FIFO keeps the DMA
            # fetch position close to the raster, which the HAL's tear-free page flip
            # (fb_present: retarget at DMA frame wrap) relies on.
            self.add_video_framebuffer(phy=vout, timings=(TIMINGS_NAME, APF_TIMINGS),
                                       clock_domain="vid", format="rgb332",
                                       fifo_depth=8192)

            vpads = platform.request("video")
            self.sync.vid += [
                vpads.de.eq(vout.de),
                vpads.hsync.eq(vout.hsync),
                vpads.vsync.eq(vout.vsync),
                vpads.r.eq(Mux(vout.de, vout.r, 0)),   # blank RGB outside active
                vpads.g.eq(Mux(vout.de, vout.g, 0)),
                vpads.b.eq(Mux(vout.de, vout.b, 0)),
            ]

            # vblank toggle CSR (from the video vsync) for the HAL's fb_present wait.
            self.vblank = CSRStatus(1)
            vs_d = Signal(); vbl = Signal()
            self.sync.vid += [vs_d.eq(vout.vsync), If(vout.vsync & ~vs_d, vbl.eq(~vbl))]
            self.specials += MultiReg(vbl, self.vblank.status, "sys")

            # Sticky underflow flag: the scanout FIFO starving during ACTIVE video
            # (valid low while de high) stalls the raster the APF scaler expects to be
            # rock-steady — without this CSR that failure is invisible. valid is low
            # during blanking by design, hence the & de qualifier.
            self.vfb_underflow = CSRStatus(1)
            uf = Signal()
            self.sync.vid += If(vout.de & ~vout.valid, uf.eq(1))
            self.specials += MultiReg(uf, self.vfb_underflow.status, "sys")

        # Uptime counter: backs the HAL's sys_ticks_us()/sys_delay_us(). Without it
        # the generated csr.h has no timer0_uptime_* and sys_ticks_us() silently
        # compiles to `return 0` -> sys_delay_us() spins forever.
        self.timer0.add_uptime()

        # CPU boots from ROM.
        self.cpu.set_reset_address(self.mem_map["rom"])

        # Simulation: drive the sim harness trace-enable pad (litex_sim does this).
        # Without it, sim_trace is a floating input and the harness mis-binds pads.
        if sim:
            self.comb += self.platform.trace.eq(1)


# -----------------------------------------------------------------------------
# Build entry point
# -----------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="Pocket Stage-1 SoC")
    parser.add_argument("--sim",      action="store_true", help="Verilator simulation target")
    parser.add_argument("--build",    action="store_true", help="run the toolchain (Quartus / Verilator)")
    parser.add_argument("--firmware", default=None,        help="firmware .bin to initialise ROM with")
    parser.add_argument("--sys-clk-freq", default=SYS_CLK_FREQ, type=float, help="hardware sys_clk (Hz)")
    parser.add_argument("--output-dir",   default=None, help="build output directory")
    parser.add_argument("--trace",        action="store_true", help="dump a VCD trace (sim)")
    parser.add_argument("--trace-end",    default="-1", help="trace end time (ps)")
    args = parser.parse_args()

    output_dir = args.output_dir or ("build/sim" if args.sim else "build/pocket")

    soc = PocketSoC(
        sim          = args.sim,
        sys_clk_freq = int(args.sys_clk_freq),
        firmware     = args.firmware,
    )
    # compile_software=True builds LiteX's libc/libbase/libcompiler_rt (and a BIOS
    # we don't use) so our separate firmware can link against them. Our firmware is
    # baked into ROM via integrated_rom_init, overriding the unused BIOS.
    builder = Builder(soc, output_dir=output_dir, csr_csv="csr.csv")

    if args.sim:
        from litex.build.sim.config import SimConfig
        sim_config = SimConfig()
        sim_config.add_clocker("sys_clk", freq_hz=int(1e6))
        sim_config.add_clocker("vclk",    freq_hz=int(1e6))  # framebuffer read-port clock
        sim_config.add_module("serial2console", "serial")
        builder.build(run=args.build, sim_config=sim_config,
                      interactive=False,
                      trace=args.trace, trace_end=int(args.trace_end))
    else:
        builder.build(run=args.build)


if __name__ == "__main__":
    main()
