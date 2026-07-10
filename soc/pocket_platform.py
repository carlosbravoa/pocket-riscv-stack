#
# Analogue Pocket - Cyclone V FPGA LiteX platform
#
# Target device is the Pocket's FPGA: 5CEBA4F23C8 (Cyclone V, 484-FBGA, speed grade 8).
# Device / pin facts are taken from the Analogue core-template ap_core.qsf.
#
# For Stage 1 this describes a *standalone* SoC top (its own clock + a debug UART)
# so we can prove the SoC synthesises and closes timing on the real device.
# Stage 2 re-targets the same SoC as a submodule inside Analogue's core_top / APF layer,
# where these signals are wired to the APF bridge, the dbg_tx/dbg_rx UART and video.
#
# SPDX-License-Identifier: BSD-2-Clause

from litex.build.generic_platform import Pins, IOStandard, Subsignal
from litex.build.altera import AlteraPlatform

# Pocket FPGA device.
DEVICE = "5CEBA4F23C8"

# clk_74a: 74.25 MHz primary reference (apf_top mainclk1), PIN_V15, 3.3-V LVCMOS.
CLK74A_PERIOD_NS = 13.468  # -> 74.25 MHz

_io = [
    # Primary 74.25 MHz clock input (Analogue clk_74a).
    ("clk74a", 0, Pins("V15"), IOStandard("3.3-V LVCMOS")),

    # External reset, active HIGH (core_top drives ~reset_n). Resets the SoC PLL,
    # whose lock loss propagates as a clean chip-wide reset — so the Pocket host/menu
    # reset actually resets the CPU and DMA. Placeholder pin (module build).
    ("rst", 0, Pins("RSTI"), IOStandard("3.3-V LVCMOS")),

    # Debug UART. In the standalone Stage-1 build these are left for the fitter to
    # place (any free I/O); Stage 2 connects tx/rx to core_top's dbg_tx/dbg_rx
    # (the 6515D breakout USB-UART / DevKey path).
    ("serial", 0,
        Subsignal("tx", Pins("R16")),   # placeholder free I/O; refined in Stage 2
        Subsignal("rx", Pins("R17")),
        IOStandard("3.3-V LVCMOS"),
    ),

    # Diagnostic output register (Stage 2): the RISC-V writes a status word here;
    # core_top renders it on the Pocket LCD as the no-JTAG "hello". When the SoC is
    # a standalone top these are just extra output pins; when it is instantiated as
    # a submodule of core_top, this becomes the `diag[31:0]` module port.
    ("diag", 0, Pins(" ".join(f"D{i}" for i in range(32))), IOStandard("3.3-V LVCMOS")),

    # Controller inputs: APF cont1_key/cont2_key pass through core_top unchanged
    # ([15:0] buttons, [31:28] controller type; synchronous to clk_74a — the SoC
    # synchronizes into sys). Placeholder pins (module build, never placed).
    ("cont1", 0, Pins(" ".join(f"C1K{i}" for i in range(32))), IOStandard("3.3-V LVCMOS")),
    ("cont2", 0, Pins(" ".join(f"C2K{i}" for i in range(32))), IOStandard("3.3-V LVCMOS")),
    ("joy1",  0, Pins(" ".join(f"C1J{i}" for i in range(32))), IOStandard("3.3-V LVCMOS")),
    ("joy2",  0, Pins(" ".join(f"C2J{i}" for i in range(32))), IOStandard("3.3-V LVCMOS")),

    # OPL3 register bus (fork): one bus write per toggle, {A[1:0], D[7:0]}.
    ("opl", 0,
        Subsignal("cmd", Pins(" ".join(f"OPC{i}" for i in range(10)))),
        Subsignal("wr",  Pins(1)),
        IOStandard("3.3-V LVCMOS"),
    ),

    # Game-exit protocol: exit toggle out; boot_skip back in (core_top keeps the
    # skip-autoload flag outside the SoC reset domain).
    ("exit",      0, Pins("XT0"), IOStandard("3.3-V LVCMOS")),
    ("boot_skip", 0, Pins("BS0"), IOStandard("3.3-V LVCMOS")),

    # Save memory access (4 KB BRAM in core_top, 16-bit words): toggle handshake.
    ("save", 0,
        Subsignal("adr",  Pins(" ".join(f"SVA{i}" for i in range(11)))),
        Subsignal("wdat", Pins(" ".join(f"SVW{i}" for i in range(16)))),
        Subsignal("wr",   Pins(1)),
        Subsignal("rd",   Pins(1)),
        Subsignal("rdat", Pins(" ".join(f"SVR{i}" for i in range(16)))),
        Subsignal("ack",  Pins(1)),
        IOStandard("3.3-V LVCMOS"),
    ),

    # Audio: current 48 kHz stereo sample pair, registered in the vid (12.288 MHz)
    # domain. core_top feeds these to sound_i2s (clk_audio = the same 12.288 clock,
    # so the handoff is same-domain). Signed 16-bit. Placeholder pins (module build).
    ("audio", 0,
        Subsignal("l", Pins(" ".join(f"AL{i}" for i in range(16)))),
        Subsignal("r", Pins(" ".join(f"AR{i}" for i in range(16)))),
        IOStandard("3.3-V LVCMOS"),
    ),

    # Pak loading (deferred APF data slot): 16-bit file words arriving in the vid
    # (12.288 MHz) domain from core_top's data_loader, DMA'd into main_ram; plus
    # the command channel to core_top's target_dataslot_read FSM (req toggle +
    # quasi-static params out; busy/err/size back, clk_74a domain, SoC syncs).
    ("loader", 0,
        Subsignal("en",   Pins(1)),
        Subsignal("addr", Pins(" ".join(f"LA{i}" for i in range(28)))),
        Subsignal("data", Pins(" ".join(f"LD{i}" for i in range(16)))),
        IOStandard("3.3-V LVCMOS"),
    ),
    ("pak", 0,
        Subsignal("req",    Pins(1)),
        Subsignal("wreq",   Pins(1)),
        Subsignal("id",     Pins(" ".join(f"PI{i}" for i in range(16)))),
        Subsignal("dtaddr", Pins(" ".join(f"PT{i}" for i in range(10)))),
        Subsignal("offset", Pins(" ".join(f"PO{i}" for i in range(32)))),
        Subsignal("length", Pins(" ".join(f"PL{i}" for i in range(32)))),
        Subsignal("busy",   Pins(1)),
        Subsignal("err",    Pins("PE0 PE1 PE2")),
        Subsignal("size",   Pins(" ".join(f"PS{i}" for i in range(32)))),
        IOStandard("3.3-V LVCMOS"),
    ),

    # Framebuffer video read port (Stage 2/3). When the SoC is a submodule of
    # core_top these become module ports: vclk (pixel clock in), fb_radr (word
    # address in), fb_rdat (32-bit pixel word out). Names are placeholders; never
    # placed (the SoC is built as a module, run=False).
    ("vclk",    0, Pins("E1"),  IOStandard("3.3-V LVCMOS")),
    # Video output stream (Stage 4): LiteX VideoFramebuffer -> these module ports ->
    # core_top -> APF video pins. Active-high hsync/vsync (no _n) to match the APF.
    ("video", 0,
        Subsignal("de",    Pins("VDE")),
        Subsignal("hsync", Pins("VHS")),
        Subsignal("vsync", Pins("VVS")),
        Subsignal("r",     Pins(" ".join(f"VR{i}" for i in range(8)))),
        Subsignal("g",     Pins(" ".join(f"VG{i}" for i in range(8)))),
        Subsignal("b",     Pins(" ".join(f"VB{i}" for i in range(8)))),
        IOStandard("3.3-V LVCMOS"),
    ),

    # External SDRAM (Stage 4). These become SoC-module ports wired to core_top's
    # dram_* pins. The Pocket SDRAM has NO cs_n pin (tied low on the board);
    # GENSDRPHY treats cs_n as optional. dram_clk is driven from the phase-shifted
    # PLL output. Pin names are placeholders (module build, run=False, never placed).
    ("sdram", 0,
        Subsignal("a",     Pins(" ".join(f"SA{i}"  for i in range(13)))),
        Subsignal("ba",    Pins("SBA0 SBA1")),
        Subsignal("dq",    Pins(" ".join(f"SDQ{i}" for i in range(16)))),
        Subsignal("dm",    Pins("SDM0 SDM1")),
        Subsignal("ras_n", Pins("SRAS")),
        Subsignal("cas_n", Pins("SCAS")),
        Subsignal("we_n",  Pins("SWE")),
        Subsignal("cke",   Pins("SCKE")),
        IOStandard("3.3-V LVCMOS"),
    ),
    ("dram_clk", 0, Pins("SCLK"), IOStandard("3.3-V LVCMOS")),
]


class Platform(AlteraPlatform):
    default_clk_name   = "clk74a"
    default_clk_period = CLK74A_PERIOD_NS

    def __init__(self, toolchain="quartus"):
        AlteraPlatform.__init__(self, DEVICE, _io, toolchain=toolchain)

    def do_finalize(self, fragment):
        AlteraPlatform.do_finalize(self, fragment)
        self.add_period_constraint(self.lookup_request("clk74a", loose=True),
                                   self.default_clk_period)
