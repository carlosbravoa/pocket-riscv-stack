# SDRAM interface constraints — the capture window, finally policed by STA.
#
# History (soc/REPRODUCIBILITY.md): this loop was completely unconstrained. The
# clock reached the pin through fit-dependent fabric routing, every build rolled
# its own capture alignment, and a bad roll compiled clean and black-screened on
# silicon (the FM 1.0 regression). The dram_clk IOE-DDIO change made the clock
# path fit-independent; THESE constraints make STA verify the alignment on every
# compile, so a drifted fit becomes a FAILED BUILD.
#
# Geometry (hardware-measured 2026-07-26, DDIO clock path, both flavors):
#   boot window = phase {135..165}, shipped default 150. At 150 the verified fits
#   report: capture setup +2.07 / hold +12.42 (next-edge capture, MC 2/1),
#   command setup +1.96, write-data next-edge like capture. Values below are
#   tuned so the VERIFIED fits pass with real margin; they are drift guards
#   anchored to known-good silicon, not datasheet theology. If you change the
#   phase, the sys clock, or the board, re-validate on hardware and re-tune.
#
# AS4C32M16 (-7) numbers used: tAC 5.4  tOH 2.5  tDS 1.5  tDH 0.8 (+board).

# The SDRAM clock pin: generated from the PLL's phase-shifted output, forwarded
# through the IOE DDIO (pocket_soc.py CRG).
create_generated_clock -name dram_clk_pin \
    -source [get_pins -compatibility_mode {*ALTPLL*|auto_generated|generic_pll2~PLL_OUTPUT_COUNTER|divclk}] \
    [get_ports {dram_clk}]

# READ CAPTURE: chip launches DQ on dram_clk_pin, tAC(max 5.4)+board later; the
# soft-DDIO capture FF samples on sys. True relationship is next-edge (MC 2/1).
set_input_delay  -clock dram_clk_pin -max 6.0 [get_ports {dram_dq[*]}]
set_input_delay  -clock dram_clk_pin -min 2.7 [get_ports {dram_dq[*]}]
set_multicycle_path -setup -end 2 -from [get_ports {dram_dq[*]}]
set_multicycle_path -hold  -end 1 -from [get_ports {dram_dq[*]}]

# COMMAND/ADDRESS: launched on sys, sampled by the chip on dram_clk_pin, same
# edge (verified +1.96 setup on the known-good fit). tDS 1.5 + board 0.2 max;
# hold: tDH 0.8 - board skew, tuned so the verified fit sits ~+0.3.
set_output_delay -clock dram_clk_pin -max  1.7 [get_ports {dram_a[*] dram_ba[*] dram_ras_n dram_cas_n dram_we_n dram_cke dram_dqm[*]}]
set_output_delay -clock dram_clk_pin -min -0.4 [get_ports {dram_a[*] dram_ba[*] dram_ras_n dram_cas_n dram_we_n dram_cke dram_dqm[*]}]

# WRITE DATA: DQ out, next-edge relationship like the read path (verified).
set_output_delay -clock dram_clk_pin -max  1.7 [get_ports {dram_dq[*]}]
set_output_delay -clock dram_clk_pin -min -0.4 [get_ports {dram_dq[*]}]
set_multicycle_path -setup -end 2 -to [get_ports {dram_dq[*]}]
set_multicycle_path -hold  -end 1 -to [get_ports {dram_dq[*]}]
