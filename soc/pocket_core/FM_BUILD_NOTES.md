# FM flavor build — the fragile parts (READ before rebuilding FM)

The FM (RiscvStackFM) bitstream at **74.25 MHz / DRAM capture phase 210** is
**timing-marginal on the SDRAM read path**, and the constraint that was supposed
to make it robust does not work. This is why an FM rebuild can be rock-solid one
time and boot to garbage / a frozen picker the next, from the *same source*.
Written 2026-07-15 after two 1.0 FM rebuilds regressed while base stayed solid.

## Root cause

- The Pocket SDRAM the SoC uses is `dram_dq[*]` (the APF `dram_*` bus; `cram0_dq`
  / `cram1_dq` are tied to Z and unused). At 74.25 MHz the DQ read must be
  captured by a phase-shifted clock (`cd_sys_ps`, phase 210°).
- `ap_core.qsf` has `set_instance_assignment -name FAST_INPUT_REGISTER ON -to
  dram_dq[*]` (and FAST_OUTPUT_REGISTER) — meant to pack the DQ capture flop into
  the IO cell for deterministic tight timing. **Quartus IGNORES it** (Fitter
  report → "Ignored Assignments"; Warning 176250). The LiteX SDR PHY's capture
  register is not IO-packable here, so the assignment has *never* taken effect —
  including in the builds that happened to work.
- With no forced IO capture, the SDRAM read timing depends on **PLL phase +
  wherever the Fitter happens to place the capture logic**. It has margin at the
  base flavor's phase 150 (base is reliably fine) but is **on the edge at the FM
  flavor's phase 210**.

## Consequence: FM builds are fit-sensitive

Any change that alters the netlist — even a trivial one like adding a CSR
(`abi_version` did exactly this for the 1.0 attempt) — makes the Fitter
re-place everything, which can move the marginal SDRAM capture from "works" to
"fails". `fm-v0.24.0` (commit `3e29631`) got a good placement and was rock
solid; the 1.0 rebuilds landed marginal (one froze at the first `sys_delay_us`
in the picker, one showed DRAM garbage).

**So: do not assume an FM rebuild is good because it compiled with 0 errors.**
It must be hardware-tested, and a good result is not guaranteed to reproduce.

## The build recipe (what IS deterministic)

```sh
# from the FM tree (opl3 branch checkout OR worktree — build_core.sh is now
# worktree-relative), with . ../env.sh sourced:
cd soc
rm -rf build/pocket pocket_core/{db,incremental_db,qdb,output_files}   # CLEAN — stale netlist/init mix causes bad fits
python pocket_soc.py --output-dir build/pocket                         # phase 210 = opl3 default (RVSTACK_DRAM_PHASE)
make -C firmware BUILD_DIR="$PWD/build/pocket"
python pocket_soc.py --firmware firmware/firmware.bin --output-dir build/pocket
cd pocket_core && VER=x.y.z ./build_core.sh                            # Quartus + package
# verify: pll CLK1_PHASE_SHIFT = 7856 (=210deg @74.25) in build/pocket/gateware/pocket_platform.v
```

Base flavor default phase is 150 (`CLK1_PHASE_SHIFT` 5611). Do NOT let
`RVSTACK_DRAM_PHASE` leak between builds (each Bash shell is fresh, so it won't
unless you export it).

## To make FM builds REPEATABLE (future work — needs hardware)

The marginality must be fixed at the hardware-in-the-loop level; it cannot be
verified remotely. Options, best first:

1. **Re-sweep the DRAM capture phase on hardware** and pick the *center* of the
   passing window (not an edge) — `soc/fm_clock_kit.sh` was the sweep tool
   (currently empty; reconstruct it). Record the winning phase as the opl3
   default AND in this file.
2. **Get the DQ capture into the IO cell for real** — the LiteX SDR PHY output
   register scheme needs adjusting so `FAST_INPUT_REGISTER` is packable, or add
   an explicit input DDIO/register in `core_top` on the `dram_dq` path. This is
   the proper fix and would make FM as robust as base.
3. **Drop the FM clock** (e.g. 66 MHz) if a robust 74.25 can't be found — trades
   the clock bump for reliability.

Until one of these lands, treat every FM bitstream as "test on hardware, and it
may not reproduce."
