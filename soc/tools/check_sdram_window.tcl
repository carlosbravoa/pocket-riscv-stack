# SDRAM capture-window gate — run by build_core.sh after every Quartus compile.
#
#   quartus_sta -t ../tools/check_sdram_window.tcl        (from soc/pocket_core)
#
# Verifies, in both timing corners, that the constrained SDRAM interface
# (core/sdram_window.sdc) still sits inside the hardware-validated envelope.
# The floors leave ~1 ns of setup drift and ~0.15 ns of hold drift from the
# 2026-07-26 verified fits (setup ~1.96..2.95, hold ~0.25..0.30 slow) before the
# build FAILS — a drifted fit becomes a broken build on the desk, not a black
# screen on the Pocket. Background: soc/REPRODUCIBILITY.md.
#
# Exit code 0 = inside the envelope; 1 = drifted (build must abort).

set SETUP_FLOOR 1.0
set HOLD_FLOOR  0.03

set failures 0

proc check {label kind floor dir ports} {
    global failures
    if {$dir eq "from"} {
        set res [report_timing -$kind -npaths 1 -quiet -from $ports]
    } else {
        set res [report_timing -$kind -npaths 1 -quiet -to $ports]
    }
    if {[lindex $res 0] == 0} {
        puts "SDRAM-WINDOW: $label ($kind): NO PATHS - constraints not applied?!"
        incr failures
        return
    }
    set s [lindex $res 1]
    set verdict [expr {$s >= $floor ? "ok" : "DRIFTED"}]
    puts [format "SDRAM-WINDOW: %-24s %-5s slack %+7.3f  floor %+5.2f  %s" \
          $label $kind $s $floor $verdict]
    if {$s < $floor} { incr failures }
}

project_open ap_core -revision ap_core

foreach model {slow fast} {
    create_timing_netlist -model $model
    read_sdc
    update_timing_netlist
    puts "SDRAM-WINDOW: ==== $model corner ===="
    set dq  [get_ports {dram_dq[*]}]
    set cmd [get_ports {dram_a[*] dram_ba[*] dram_ras_n dram_cas_n dram_we_n}]
    check "capture (dq->FPGA)" setup $SETUP_FLOOR from $dq
    check "capture (dq->FPGA)" hold  $HOLD_FLOOR  from $dq
    check "command/address"    setup $SETUP_FLOOR to   $cmd
    check "command/address"    hold  $HOLD_FLOOR  to   $cmd
    check "write data"         setup $SETUP_FLOOR to   $dq
    check "write data"         hold  $HOLD_FLOOR  to   $dq
    delete_timing_netlist
}

# ---- global closure: the whole design must meet slow-corner setup ----------
# (Quartus "compiles successfully" with violated timing; this catches it. The
# voice-mixer integration taught us that lesson: its first fit passed the
# window checks while the mixer's own paths missed 74.25 MHz by 1 ns.)
create_timing_netlist -model slow
read_sdc
update_timing_netlist
set dom [get_clock_domain_info -setup]
foreach d $dom {
    set cname [lindex $d 0]
    set slack [lindex $d 1]
    if {$slack < 0} {
        puts [format "SDRAM-WINDOW: GLOBAL setup %-50s slack %+7.3f  DRIFTED" $cname $slack]
        incr failures
    }
}
if {$failures == 0} { puts "SDRAM-WINDOW: GLOBAL slow-corner setup clean in every domain" }
delete_timing_netlist

project_close

if {$failures > 0} {
    puts "SDRAM-WINDOW: FAIL - $failures metric(s) outside the validated envelope."
    puts "SDRAM-WINDOW: Do NOT flash this bitstream. See soc/REPRODUCIBILITY.md."
    exit 1
}
puts "SDRAM-WINDOW: PASS - capture window inside the hardware-validated envelope."
exit 0
