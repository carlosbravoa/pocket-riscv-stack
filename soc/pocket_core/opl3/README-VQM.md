# OPL3 as a pre-synthesized VQM netlist — why and how

The core instantiates `opl3_synth.vqm` (atoms), NOT the RTL in this directory.

**Why:** in-context RTL synthesis under Quartus 25.1std Lite persistently
mis-analyzed this design — it "proved" `sample_l == 0` and removed the FM
output path (opl_l/opl_r "stuck at GND") through every countermeasure: RAM
style overrides, shift-register extraction off, de-uniqued case statements,
preserve attributes, design partitions (which Lite silently discards). The
same RTL compiled STANDALONE survives, and Verilator proves it plays. So we
ship the standalone compile's atoms; they are opaque to the in-context
optimizer (with ADV_NETLIST_OPT_SYNTH_WYSIWYG_REMAP OFF in ap_core.qsf).
Hardware-confirmed working 2026-07-10 (v0.15.6).

**Regenerate after editing the RTL** (also re-run the Verilator regression in
the fork's history first):

```sh
mkdir /tmp/opl3vqm && cd /tmp/opl3vqm
cp <this dir>/*.sv <this dir>/*.v <this dir>/opl3vqm.qsf .
quartus_map opl3vqm -c opl3vqm
quartus_cdb opl3vqm -c opl3vqm --vqm=opl3_synth.vqm
cp opl3_synth.vqm <this dir>/
rm <this dir>/opl3vqm.*.mif && cp db/*.mif <this dir>/
sed -i 's|db/opl3vqm\.|opl3/opl3vqm.|g' <this dir>/opl3_synth.vqm
# then: rm -rf pocket_core/{db,incremental_db,output_files} and rebuild
```

Notes: opl3vqm.qsf keeps DSP off (`DSP_BLOCK_BALANCING "LOGIC ELEMENTS"`) —
the VQM reader rejects the mac atom config (Error 15649). The .mif files ARE
the LUT/RAM contents; the vqm references them by opl3/-relative path.
