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
# 50 MHz hardware-confirmed 2026-07-09 (v0.11.0); sys-domain Fmax when pushed ~73 MHz,
# SDR read-capture margin ~3 ns at 180deg — do not raise casually. 25 MHz remains the
# safe fallback (--sys-clk-freq 25e6).
SYS_CLK_FREQ = int(66e6)   # 74.25 * 8/9; v0.18.1 speed stage (50 MHz confirmed, Fmax 69-78)


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

# main_ram layout (byte offsets): framebuffer pages at +0/+0x20000; deferred
# data-slot pulls default to the assets region at +1 MB; game binaries are pulled
# to +4 MB and executed there (the ROM is just a bootloader).
PAK_RAM_OFFSET  = 0x0010_0000
GAME_RAM_OFFSET = 0x0040_0000
SAVE_RAM_OFFSET = 0x0200_0000   # per-game save data staging area (1 MB budget)


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


class _CRGSimCore(LiteXModule):
    """simcore CRG: no vendor PLL primitives (Verilator). sys = clk74a directly
    (the generated headers carry 74.25e6 so timer-based delays stay correct)."""
    def __init__(self, platform):
        self.rst    = Signal()
        self.cd_sys = ClockDomain()
        self.comb += self.cd_sys.clk.eq(platform.request("clk74a"))
        from migen.genlib.resetsync import AsyncResetSynchronizer
        self.specials += AsyncResetSynchronizer(
            self.cd_sys, self.rst | platform.request("rst"))


# -----------------------------------------------------------------------------
# SoC
# -----------------------------------------------------------------------------

class PocketSoC(SoCCore):
    def __init__(self, sim=False, simcore=False, sys_clk_freq=SYS_CLK_FREQ,
                 firmware=None, with_sdram=True, **kwargs):
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
                ("cont1", 0, Pins(32)),
                ("cont2", 0, Pins(32)),
                ("joy1", 0, Pins(32)),
                ("joy2", 0, Pins(32)),
                ("hwfeat", 0, Pins(32)),
                ("exit", 0, Pins(1)),
                ("boot_skip", 0, Pins(1)),
                ("opl", 0,
                    Subsignal("cmd", Pins(10)),
                    Subsignal("wr",  Pins(1)),
                    Subsignal("dbg", Pins(16)),
                ),
                ("save", 0,
                    Subsignal("adr",  Pins(14)),
                    Subsignal("wdat", Pins(16)),
                    Subsignal("wr",   Pins(1)),
                    Subsignal("rd",   Pins(1)),
                    Subsignal("rdat", Pins(16)),
                    Subsignal("ack",  Pins(1)),
                ),
                ("audio", 0,
                    Subsignal("l", Pins(16)),
                    Subsignal("r", Pins(16)),
                ),
                ("loader", 0,
                    Subsignal("en",   Pins(1)),
                    Subsignal("addr", Pins(28)),
                    Subsignal("data", Pins(16)),
                ),
                ("pak", 0,
                    Subsignal("req",    Pins(1)),
                    Subsignal("wreq",   Pins(1)),
                    Subsignal("ofreq",  Pins(1)),
                    Subsignal("gfreq",  Pins(1)),
                    Subsignal("szset",  Pins(1)),
                    Subsignal("dtsize", Pins(32)),
                    Subsignal("id",     Pins(16)),
                    Subsignal("dtaddr", Pins(10)),
                    Subsignal("offset", Pins(32)),
                    Subsignal("length", Pins(32)),
                    Subsignal("busy",   Pins(1)),
                    Subsignal("err",    Pins(3)),
                    Subsignal("size",   Pins(32)),
                ),
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
        elif simcore:
            # Full-system simulation: the REAL module pads (core_top instantiates
            # this exactly like the hardware SoC) but no vendor primitives — DRAM
            # is LiteDRAM's SDRAMPHYModel (memory lives inside the verilog).
            import pocket_platform
            platform     = pocket_platform.Platform()
            sys_clk_freq = int(74.25e6)   # sys = clk74a (see _CRGSimCore)
            self.crg     = _CRGSimCore(platform)
            uart_name    = "serial"
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
            if sim or simcore:
                from litedram.phy.model import SDRAMPHYModel
                # simcore MUST match the hardware port geometry (16-bit, like
                # GENSDRPHY on the x16 chip): the pak DMA's address math
                # (pak_dst[1:] = halfword address) is written for it. A 32-bit
                # model port scatters DMA'd games across wrong offsets — the
                # game's first fetch traps with mcause=2 (found by soc/sim).
                self.sdrphy = SDRAMPHYModel(module=sdram_module,
                                            data_width=16 if simcore else 32,
                                            clk_freq=sys_clk_freq)
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

            # Palette: 256 x RGB888 lookup between the framebuffer byte and the
            # pads. LiteX's rgb332 expansion puts index[7:5] in r[7:5], [4:2] in
            # g[7:5], [1:0] in b[7:6] — reconstruct the index losslessly, look it
            # up in a dual-clock BRAM (CPU writes in sys, scanout reads in vid).
            # Init = the exact same rgb332 expansion, so a game that never calls
            # palette_set() renders identically to the pre-palette hardware.
            def _rgb332(i):
                return (((i >> 5) & 7) << 21) | (((i >> 2) & 7) << 13) | ((i & 3) << 6)
            pal = Memory(24, 256, init=[_rgb332(i) for i in range(256)])
            pal_wr = pal.get_port(write_capable=True, clock_domain="sys")
            pal_rd = pal.get_port(clock_domain="vid")           # sync read: 1 cycle
            self.specials += pal, pal_wr, pal_rd
            self.palette = CSRStorage(32)   # write = {index[31:24], R[23:16], G[15:8], B[7:0]}
            self.comb += [
                pal_wr.adr.eq(self.palette.storage[24:32]),
                pal_wr.dat_w.eq(self.palette.storage[0:24]),
                pal_wr.we.eq(self.palette.re),
                pal_rd.adr.eq(Cat(vout.b[6:8], vout.g[5:8], vout.r[5:8])),
            ]

            # Output regs: the palette read adds one vid cycle on the RGB path, so
            # de/hsync/vsync get a matching delay stage to stay pixel-aligned.
            vpads = platform.request("video")
            de_d, hs_d, vs_d1 = Signal(), Signal(), Signal()
            self.sync.vid += [
                de_d.eq(vout.de), hs_d.eq(vout.hsync), vs_d1.eq(vout.vsync),
                vpads.de.eq(de_d),
                vpads.hsync.eq(hs_d),
                vpads.vsync.eq(vs_d1),
                vpads.r.eq(Mux(de_d, pal_rd.dat_r[16:24], 0)),  # blank outside active
                vpads.g.eq(Mux(de_d, pal_rd.dat_r[ 8:16], 0)),
                vpads.b.eq(Mux(de_d, pal_rd.dat_r[ 0: 8], 0)),
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

            # --- Audio: CPU-fed 48 kHz stereo PCM stream ------------------------
            # CSR-pushed samples (L[15:0]|R[31:16], signed) -> sys FIFO -> CDC ->
            # vid-domain drain at EXACTLY 12.288 MHz / 256 = 48 kHz -> audio pads.
            # core_top's sound_i2s (clk_audio = the same 12.288 clock) serializes
            # them to the APF DAC. On underrun the last sample is held (no click).
            # Depth 2048 frames ~= 42 ms: two display frames of slack for a game
            # loop that tops the FIFO up once per frame.
            apads = platform.request("audio")
            audio_fifo = stream.SyncFIFO([("data", 32)], depth=2048, buffered=True)
            audio_cdc  = stream.ClockDomainCrossing([("data", 32)],
                                                    cd_from="sys", cd_to="vid", depth=8)
            self.submodules += audio_fifo, audio_cdc
            self.audio_sample = CSRStorage(32)  # write = push one stereo frame
            self.audio_level  = CSRStatus(16)   # sys-side FIFO fill, for backpressure
            self.comb += [
                audio_fifo.sink.valid.eq(self.audio_sample.re),
                audio_fifo.sink.data.eq(self.audio_sample.storage),
                audio_fifo.source.connect(audio_cdc.sink),
                self.audio_level.status.eq(audio_fifo.level),
            ]
            acnt = Signal(8)                    # /256 of 12.288 MHz = 48 kHz
            al, ar = Signal(16), Signal(16)
            self.sync.vid += [
                acnt.eq(acnt + 1),
                If((acnt == 0) & audio_cdc.source.valid,
                    al.eq(audio_cdc.source.data[0:16]),
                    ar.eq(audio_cdc.source.data[16:32]),
                ),
            ]
            self.comb += [
                audio_cdc.source.ready.eq(acnt == 0),
                apads.l.eq(al),
                apads.r.eq(ar),
            ]

            # --- Pak: deferred APF data-slot pull into main_ram -----------------
            # core_top's data_loader delivers the file as 16-bit words + byte
            # addresses in the vid domain; CDC to sys, then DMA-write into
            # main_ram at PAK_RAM_OFFSET. The command channel (req toggle +
            # offset/length; busy/err/size back) drives core_top's
            # target_dataslot_read FSM. The HAL orchestrates chunked pulls.
            from litedram.frontend.dma import LiteDRAMDMAWriter, LiteDRAMDMAReader
            lpads = platform.request("loader")
            ppads = platform.request("pak")
            pak_dma = LiteDRAMDMAWriter(self.sdram.crossbar.get_port(), fifo_depth=16)
            loader_cdc = stream.ClockDomainCrossing(
                [("addr", 28), ("data", 16)], cd_from="vid", cd_to="sys", depth=64)
            self.submodules += pak_dma, loader_cdc
            # Destination (byte offset in main_ram, 2-aligned) for the CURRENT pull.
            # The host bridge-writes each chunk at bridgeaddr+0.., so without this
            # every chunk would land at the same place (the v0.10.0 bug).
            self.pak_dst = CSRStorage(32, reset=PAK_RAM_OFFSET)
            self.comb += [
                # vid side: data_loader strobes one word per ~5 cycles; depth-64
                # CDC absorbs bursts (sys side drains ~4x faster than vid fills).
                loader_cdc.sink.valid.eq(lpads.en),
                loader_cdc.sink.addr.eq(lpads.addr),
                loader_cdc.sink.data.eq(lpads.data),
                # sys side: byte addr -> 16-bit-word addr at the chosen destination.
                pak_dma.sink.valid.eq(loader_cdc.source.valid),
                loader_cdc.source.ready.eq(pak_dma.sink.ready),
                pak_dma.sink.address.eq(self.pak_dst.storage[1:] + loader_cdc.source.addr[1:]),
                pak_dma.sink.data.eq(loader_cdc.source.data),
            ]
            self.pak_req    = CSRStorage(1)   # toggle = issue one dataslot read
            self.pak_wreq   = CSRStorage(1)   # toggle = issue one dataslot WRITE (save commit)
            self.pak_ofreq  = CSRStorage(1)   # toggle = issue one dataslot OPENFILE (bind/create save file)
            self.pak_gfreq  = CSRStorage(1)   # toggle = issue one dataslot GETFILE (bring-up diagnostic)
            self.save_szset  = CSRStorage(1)  # toggle = write save_dtsize into the datatable (host flush size)
            self.save_dtsize = CSRStorage(32) # actual save byte count for the .sav
            self.pak_id     = CSRStorage(16, reset=1)  # APF slot id for the pull
            self.pak_dtaddr = CSRStorage(10, reset=1)  # datatable addr (index*2+1) for size
            self.pak_offset = CSRStorage(32)  # slot offset (bytes), set before req
            self.pak_length = CSRStorage(32)  # length (bytes), set before req
            self.pak_busy   = CSRStatus(1)
            self.pak_err    = CSRStatus(3)    # 0=ok, 1=bad slot, 2=out of range, 7=watchdog
            self.pak_size   = CSRStatus(32)   # slot size from the APF data table
            self.comb += [
                ppads.req.eq(self.pak_req.storage),
                ppads.wreq.eq(self.pak_wreq.storage),
                ppads.ofreq.eq(self.pak_ofreq.storage),
                ppads.gfreq.eq(self.pak_gfreq.storage),
                ppads.szset.eq(self.save_szset.storage),
                ppads.dtsize.eq(self.save_dtsize.storage),
                ppads.id.eq(self.pak_id.storage),
                ppads.dtaddr.eq(self.pak_dtaddr.storage),
                ppads.offset.eq(self.pak_offset.storage),
                ppads.length.eq(self.pak_length.storage),
            ]
            self.specials += [
                MultiReg(ppads.busy, self.pak_busy.status, "sys"),
                MultiReg(ppads.err,  self.pak_err.status,  "sys"),
                MultiReg(ppads.size, self.pak_size.status, "sys"),
            ]
            self.add_constant("PAK_RAM_OFFSET",  PAK_RAM_OFFSET)   # -> generated/soc.h
            self.add_constant("GAME_RAM_OFFSET", GAME_RAM_OFFSET)  # -> generated/soc.h

            # -------------------------------------------------------------
            # Blitter: rectangular DRAM->DRAM copy engine. fbbench measured a
            # 64 KB CPU frame copy at 12.9 ms (cache-miss storm, clock-immune)
            # — this engine does it at port speed, asynchronously. Reader
            # and writer stream row by row in lockstep; addresses are in
            # 16-bit port words (API takes bytes, even-aligned). Present in
            # EVERY flavor (it lives in the shared SoC): hwfeat bit 6.
            # -------------------------------------------------------------
            blit_rd = LiteDRAMDMAReader(self.sdram.crossbar.get_port(), fifo_depth=16)
            blit_wr = LiteDRAMDMAWriter(self.sdram.crossbar.get_port(), fifo_depth=16)
            self.submodules += blit_rd, blit_wr

            self.blit_src     = CSRStorage(32)  # byte offset in main_ram, even
            self.blit_dst     = CSRStorage(32)  # byte offset in main_ram, even
            self.blit_sstride = CSRStorage(16)  # source row stride, bytes
            self.blit_dstride = CSRStorage(16)  # destination row stride, bytes
            self.blit_w       = CSRStorage(16)  # row width, bytes (even, >0)
            self.blit_h       = CSRStorage(16)  # rows (>0)
            self.blit_kick    = CSRStorage(1)   # any write starts the blit
            self.blit_busy    = CSRStatus(1)
            self.blit_flags   = CSRStorage(1)   # bit0: colorkey-0 mode

            hw_w      = Signal(15)              # width in port words
            rd_row    = Signal(16)
            wr_row    = Signal(16)
            rd_beat   = Signal(15)
            wr_beat   = Signal(15)
            rd_base   = Signal(31)              # word addr of current src row
            wr_base   = Signal(31)
            busy      = Signal()
            settle    = Signal(9)               # writer-fifo drain grace

            # Colorkey writer: a beat-level native-port writer that computes
            # byte enables from the DATA (byte==0 -> not written) and skips
            # fully-transparent beats outright. Kept SEPARATE from the proven
            # opaque DMAWriter path (present-blits) — ckey steers the reader.
            ck_port  = self.sdram.crossbar.get_port()
            ckey     = Signal()                 # latched at kick from flags
            ck_data  = Signal(16)
            ck_mask  = Signal(2)
            ck_cmd_d = Signal()                 # cmd accepted for this beat
            ck_dat_d = Signal()                 # wdata accepted for this beat
            ck_fire  = Signal()                 # beat fully issued this cycle
            ck_skip  = Signal()                 # transparent beat: no write
            self.comb += [
                ck_data.eq(blit_rd.source.data),
                ck_mask.eq(Cat(blit_rd.source.data[0:8] != 0,
                               blit_rd.source.data[8:16] != 0)),
                ck_skip.eq(ckey & (ck_mask == 0)),
                ck_port.cmd.addr.eq(wr_base + wr_beat),
                ck_port.cmd.we.eq(1),
                ck_port.wdata.data.eq(ck_data),
                ck_port.wdata.we.eq(ck_mask),
                ck_port.cmd.valid.eq(busy & ckey & blit_rd.source.valid
                                     & ~ck_skip & ~ck_cmd_d),
                ck_port.wdata.valid.eq(busy & ckey & blit_rd.source.valid
                                       & ~ck_skip & ~ck_dat_d),
                ck_fire.eq(busy & ckey & blit_rd.source.valid &
                           (ck_skip |
                            ((ck_cmd_d | (ck_port.cmd.valid & ck_port.cmd.ready)) &
                             (ck_dat_d | (ck_port.wdata.valid & ck_port.wdata.ready))))),
            ]
            self.sync += [
                If(ck_fire | ~busy,
                    ck_cmd_d.eq(0), ck_dat_d.eq(0),
                ).Else(
                    If(ck_port.cmd.valid & ck_port.cmd.ready, ck_cmd_d.eq(1)),
                    If(ck_port.wdata.valid & ck_port.wdata.ready, ck_dat_d.eq(1)),
                ),
            ]

            self.comb += [
                self.blit_busy.status.eq(busy),
                blit_rd.sink.address.eq(rd_base + rd_beat),
                blit_wr.sink.address.eq(wr_base + wr_beat),
                blit_wr.sink.data.eq(blit_rd.source.data),
                # writer consumes exactly what the reader produces, in order;
                # in colorkey mode the masked beat-writer consumes instead
                blit_wr.sink.valid.eq(blit_rd.source.valid & busy & ~ckey),
                blit_rd.source.ready.eq(busy & ((~ckey & blit_wr.sink.ready) | ck_fire)),
                # reader address issue: independent, runs ahead into its fifo
                blit_rd.sink.valid.eq(busy & (rd_row != self.blit_h.storage)),
            ]
            self.sync += [
                If(self.blit_kick.re & ~busy & (self.blit_w.storage != 0)
                   & (self.blit_h.storage != 0),
                    busy.eq(1),
                    ckey.eq(self.blit_flags.storage[0]),
                    hw_w.eq(self.blit_w.storage[1:]),
                    rd_row.eq(0), wr_row.eq(0),
                    rd_beat.eq(0), wr_beat.eq(0),
                    rd_base.eq(self.blit_src.storage[1:]),
                    wr_base.eq(self.blit_dst.storage[1:]),
                    settle.eq(0),
                ),
                # ---- reader address sequencing ----
                If(busy & blit_rd.sink.valid & blit_rd.sink.ready,
                    If(rd_beat == hw_w - 1,
                        rd_beat.eq(0),
                        rd_row.eq(rd_row + 1),
                        rd_base.eq(rd_base + self.blit_sstride.storage[1:]),
                    ).Else(
                        rd_beat.eq(rd_beat + 1),
                    ),
                ),
                # ---- writer beat sequencing (opaque handshake OR ckey beat) ----
                If((busy & blit_wr.sink.valid & blit_wr.sink.ready) | ck_fire,
                    If(wr_beat == hw_w - 1,
                        wr_beat.eq(0),
                        wr_row.eq(wr_row + 1),
                        wr_base.eq(wr_base + self.blit_dstride.storage[1:]),
                    ).Else(
                        wr_beat.eq(wr_beat + 1),
                    ),
                ),
                # ---- completion: all rows written + writer fifo grace ----
                If(busy & (wr_row == self.blit_h.storage),
                    settle.eq(settle + 1),
                    If(settle == 256,
                        busy.eq(0),
                    ),
                ),
            ]

            self.add_constant("SAVE_RAM_OFFSET", SAVE_RAM_OFFSET)  # -> generated/soc.h
            if simcore:
                # Verilator wall-clock: the stock 2 MiB boot memtest (plus its
                # 115200-baud progress spam) costs ~60M cycles. 8 KB proves the
                # same path.
                self.add_constant("MEMTEST_DATA_SIZE", 8192)
                self.add_constant("MEMTEST_ADDR_SIZE", 4096)

        # Controller inputs (APF cont1_key/cont2_key, clk_74a domain): 2-FF MultiReg
        # per bit into sys is fine for human-speed quasi-static button states. The
        # HAL's input_poll() snapshots these once per frame. joy = analog sticks.
        self.cont1 = CSRStatus(32)
        self.cont2 = CSRStatus(32)
        self.joy1  = CSRStatus(32)
        self.joy2  = CSRStatus(32)
        self.specials += MultiReg(platform.request("cont1"), self.cont1.status, "sys")
        self.specials += MultiReg(platform.request("cont2"), self.cont2.status, "sys")
        self.specials += MultiReg(platform.request("joy1"),  self.joy1.status,  "sys")
        self.specials += MultiReg(platform.request("joy2"),  self.joy2.status,  "sys")

        # OPL3 register bus (fork): each CSR write pushes one bus write to the FM
        # chip — {A[1:0], D[7:0]} — and flips the toggle automatically (.re).
        opads = platform.request("opl")
        self.opl_cmd = CSRStorage(10)
        opl_tgl = Signal()
        self.sync += If(self.opl_cmd.re, opl_tgl.eq(~opl_tgl))
        self.comb += [
            opads.cmd.eq(self.opl_cmd.storage),
            opads.wr.eq(opl_tgl),
        ]
        self.opl_dbg = CSRStatus(16)   # FM chain diagnostics from core_top
        self.specials += MultiReg(opads.dbg, self.opl_dbg.status, "sys")

        # Hardware feature ID: the FLAVOR (core_top) declares what it implements
        # (HAL_FEAT_* bits). sys_caps() reads this at runtime, so one game binary
        # runs on every family member and adapts (e.g. FM vs PCM music).
        self.hwfeat = CSRStatus(32)
        self.specials += MultiReg(platform.request("hwfeat"), self.hwfeat.status, "sys")

        # Game-exit protocol: toggling `exit` makes core_top set its (SoC-reset-
        # surviving) skip-autoload flag and pulse the SoC reset; after the reboot
        # the bootloader reads boot_skip and shows the picker instead of
        # relaunching. See sys_exit() in the HAL.
        self.exit      = CSRStorage(1)
        self.boot_skip = CSRStatus(1)
        self.comb += platform.request("exit").eq(self.exit.storage)
        self.specials += MultiReg(platform.request("boot_skip"),
                                  self.boot_skip.status, "sys")

        # Save memory (4 KB BRAM in core_top; persisted by the host as a
        # nonvolatile data slot). Word-at-a-time toggle handshake; the HAL sets
        # adr/wdat, waits a settle, toggles wr/rd, then waits for ack to flip.
        svpads = platform.request("save")
        self.save_adr  = CSRStorage(14)
        self.save_wdat = CSRStorage(16)
        self.save_wr   = CSRStorage(1)
        self.save_rd   = CSRStorage(1)
        self.save_rdat = CSRStatus(16)
        self.save_ack  = CSRStatus(1)
        self.comb += [
            svpads.adr.eq(self.save_adr.storage),
            svpads.wdat.eq(self.save_wdat.storage),
            svpads.wr.eq(self.save_wr.storage),
            svpads.rd.eq(self.save_rd.storage),
        ]
        self.specials += [
            MultiReg(svpads.rdat, self.save_rdat.status, "sys"),
            MultiReg(svpads.ack,  self.save_ack.status,  "sys"),
        ]

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
    parser.add_argument("--simcore",  action="store_true", help="emit the hw-pad SoC with model DRAM (full-system core_top sim)")
    parser.add_argument("--build",    action="store_true", help="run the toolchain (Quartus / Verilator)")
    parser.add_argument("--firmware", default=None,        help="firmware .bin to initialise ROM with")
    parser.add_argument("--sys-clk-freq", default=SYS_CLK_FREQ, type=float, help="hardware sys_clk (Hz)")
    parser.add_argument("--output-dir",   default=None, help="build output directory")
    parser.add_argument("--trace",        action="store_true", help="dump a VCD trace (sim)")
    parser.add_argument("--trace-end",    default="-1", help="trace end time (ps)")
    args = parser.parse_args()

    output_dir = args.output_dir or ("build/sim" if args.sim else
                  ("build/simcore" if args.simcore else "build/pocket"))

    soc = PocketSoC(
        sim          = args.sim,
        simcore      = args.simcore,
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
