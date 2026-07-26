# Reproducible bitstreams — why a rebuild of identical source was never the same build

Established 2026-07-24/25 by rebuilding **both** v0.24.0 flavors from their tags and
comparing against the published release artifacts. Read this before blaming DRAM
phase, Quartus, or the netlist cache for an FM rebuild that does not work.

## Result first

With the three inputs below pinned, the build is **byte-for-byte deterministic**
run to run, and it lands on the **v0.24.0 fit**:

| | base (`v0.24.0`) | FM (`fm-v0.24.0`) |
|---|---|---|
| CPU netlist name | `4b985990…` == the shipped `ap_core.qsf` | `c1f31ab4…` == the shipped `ap_core.qsf` |
| rbf_r size vs published | **+4 B** (1,613,208 vs 1,613,204) | **+8 B** (1,712,008 vs 1,712,000) |
| longest identical byte run | **1,128,030 B** | **1,185,121 B** |
| bytes equal at same offset | 72.3 % | 71.9 % |
| worst setup slack, CPU clock | 2.155 ns | **2.022 ns** |
| the same source built **unpinned** | 4.6 %, 23 KB run, −13,600 B | 3.6 %, 21 KB run, −8,400 B |

The FM row is the proof: the `fm-v0.24.0` commit message records that the shipped
bitstream "closes timing **+2.022 ns** @ 74.25/210". The pinned rebuild reports
exactly 2.022 ns. Two consecutive pinned FM builds were byte-identical
(sha256 `d6373587d617101ae685f445f338df6d223d1cc182e0abf48688828f637c3655`).

## The three hidden inputs

### 1. The CPU netlist module name is randomised on every elaboration
`litex/soc/cores/cpu/vexiiriscv/core.py`:

```python
246:    isa_map = set() if isa_map is None else set(isa_map)     # a SET
276:    vexii_args += " --with-isa=" + ",".join(isa_map)         # order = hash order
490: def generate_netlist_name():
498:        md5_hash.update(str(VexiiRiscv.vexii_args).encode('utf-8'))
506:    VexiiRiscv.netlist_name = "VexiiRiscvLitex_" + digest    # -> Verilog MODULE NAME
```

Python randomises string-set iteration order per process (`PYTHONHASHSEED`), so the
CPU's module name changes in **every** elaboration — observed twice inside a single
build (elaborate #1 `37ee99bf…`, elaborate #2 `4b864a32…`). A renamed CPU renames
every hierarchical node under it, and Quartus places the design differently.

Every other md5 input is constant (all of them were dumped and checked): `soc_args`
values, `reset_address=0`, `litedram_width=16`, `xlen=32`, the four memory regions,
`with_opensbi=False`, `with_supervisor=False`. Brute-forcing the 8! orderings against
those constants recovers the shipped names exactly:

| netlist name in the shipped `ap_core.qsf` | the ISA order that generates it |
|---|---|
| `4b9859908d5f5c6a34b6275944149541` (base v0.24.0) | `zicsr,zifencei,zmmul,zihpm,i,m,zicntr,zicbom` |
| `c1f31ab479bd88261a98a6b78f3d3184` (FM v0.24.0)   | `i,zicsr,zifencei,zihpm,m,zmmul,zicntr,zicbom` |

Two corollaries worth keeping:
- base and FM are provably the **same SoC configuration** — every hash input except
  the random order is identical between the flavors. The family-ABI claim holds.
- today's regenerated netlist is **byte-identical** to v0.24.0's once the module name
  is normalised, so VexiiRiscv, SpinalHDL, sbt and the netlist cache have **not**
  drifted. That hypothesis is dead; stop chasing it.

**Pinned by** `_isa_order` in `pocket_soc.py::_configure_vexiiriscv()` (per branch:
`main` carries the base order, `opl3` the FM one).

### 2. The SoC identifier ROM carries the elaboration timestamp
`pocket_soc.py` sets `ident = "RISC-V stack / Pocket Stage-1"` with
`ident_version = True`, so LiteX appends the build date+time. It lands in
`build/pocket/gateware/pocket_platform_mem.init` as
`"RISC-V stack / Pocket Stage-1 2026-07-24 23:31:58"`.

This is **not** cosmetic. Two FM builds (`fm-1`, `fm-2`) were identical in every other
input — same tag, same CPU netlist name, byte-identical `firmware.bin` and every other
`.init` — and differed *only* in those 50 ASCII bytes. They fitted differently:
2.214 ns / 1,703,600 B versus 2.022 ns / 1,712,008 B. One timestamp string decided
whether the build reproduced v0.24.0 or not.

**Pinned by** `SOURCE_DATE_EPOCH`, which LiteX honours at
`litex/soc/integration/soc.py:43`.

### 3. The APF build-id MIF is regenerated with a random nonce
`soc/pocket_core/apf/build_id_gen.tcl` is a Quartus `PRE_FLOW_SCRIPT_FILE`
(`ap_core.qsf:47`). It rewrites `apf/build_id.mif` on every compile with `buildDate`,
`buildTime` and `buildUnique = int(rand()*4294967295)`. That MIF initialises the
`mf_datatable` M10K instantiated at `core/core_bridge_cmd.v:565`, so those bits are in
the bitstream. Analogue documents the disable switch themselves at lines 162-171 of
that script.

**Pinned by** `soc/tools/pin_build_id.sh pin` (and `restore` to undo).

## Why byte-exact equality with an already-published rbf is impossible

The build-id nonce is `rand()`, and the identifier holds the wall-clock second of a
build run on 2026-07-13/14. Neither is recoverable. The bitstream is also compressed
(entropy ≈ 7.0 b/byte), so once two streams diverge they stay diverged. That is why
the metric that matters is the **single unbroken identical run** (1.13 MB base /
1.19 MB FM) together with the size delta (+4 / +8 B) and the matching timing slack —
not raw byte equality. An unrelated fit scores ~21-23 KB on that metric.

## What this buys going forward

The fit is no longer an uncontrollable lottery — it is a **selectable, reproducible
parameter**. The ISA order alone gives 8! = 40,320 distinct deterministic fits, and
`SOURCE_DATE_EPOCH` gives more. If an FM bitstream fails on silicon, change the pin,
rebuild, and hardware-test the next *deterministic* candidate — and when one works,
that exact bitstream is reproducible forever.

## Ruled out, with evidence

- Quartus 25.1std.0 Build 1129, installed 2026-07-05, binaries untouched since.
- `litex/litex` clean at `274c1df` (2026-07-08, predates v0.24.0); `litedram` `09ca03d`.
- `pythondata-cpu-vexiiriscv` clean at `15cfab5`; `ext/VexiiRiscv` clean at `235753e`;
  the regenerated CPU netlist is byte-identical to v0.24.0's.
- No `RVSTACK_*` env vars set. Phase confirmed in the generated PLL: base `13'd5611`
  (150°), FM `13'd7856` (210°).
- The elaborated RTL is otherwise deterministic: two pinned elaborations produced a
  `pocket_platform.v` differing **only** in a date comment and the order of `[BB:…]`
  lines inside a comment block — and still assembled to byte-identical bitstreams.
- The `abi_version` CSR (`fm-v0.24.0` → `opl3` HEAD) does not change the CPU netlist
  name: `opl3` HEAD with the pin still elaborates `c1f31ab4…`.

## Packaging note on the v0.24.0 release

Neither shipped bitstream was rebuilt at release time. Base was compiled 07-13 22:32
from commit `a687f28` (build-identical to tag `v0.24.0` — the only delta is
`soc/fm_resample/`, which base does not compile); FM was compiled 07-14 12:02 from
tag `fm-v0.24.0`. Both `core.json` files were stamped 07-14 12:26 by a repackage.
So "rebuild the tag" is the right thing to do for both.

## Verifying a rebuild against a known-good bitstream

```sh
soc/tools/compare_bitstream.py <known-good.rbf_r> <rebuilt.rbf_r>
```
Same fit → one identical run of ~1.1-1.2 MB and a size delta of a few bytes.
Different fit → longest run ~21-23 KB and a size delta of thousands of bytes.

---

# Part 2 — the FM BOOT failure (investigation of 2026-07-25)

Reproducing the fit turned out NOT to be sufficient: rebuilds that are provably the
same fit as the published FM v0.24.0 (1.19 MB unbroken identical run, and the exact
**2.022 ns** slack recorded in the fm-v0.24.0 commit message) still black-screen,
while the published bitstream boots. Base rebuilds all boot. Hardware A/B rounds
(four-core installs via `soc/tools/make_ab_test_zip.sh`) plus software forensics
eliminated, with direct evidence:

| eliminated | evidence |
|---|---|
| packaging / install path | published bitstream boots through OUR packaging (B vs A) |
| overwrite confusion | the old per-flavor zips all unpack to `Cores/bravo.RiscvStackFM/` — sequential installs silently replace each other; test cores side by side instead |
| build stamps (identifier, build-id) | natural-stamp rebuilds fail identically (FM2nat/FM3alt) |
| fitter physical synthesis | physsynth-OFF build also fails; physsynth has been ON since v0.7.0, through every rock-solid era build (it's the Analogue template default) |
| firmware | byte-identical `dab5d801…` across flavors/builds; ROM `.init` grains reconstruct `firmware.bin` exactly; stale `crt0.o` proven harmless |
| the `./build.sh hw` flow delta | its LiteX-Quartus pass fails on placeholder pin assignments at the tag (always did); its gateware outputs equal elaborate-only |
| DRAM capture phase | 4-phase hardware sweep all failed — AND the sweep was methodologically invalid: every point was a fresh fit, so phase and fit varied together |
| SDRAM IO timing as a fit variable | force-reported STA paths: DQ pin→capture arrivals and the dram_clk output delay are QUANTIZED — identical ±0.1 ns between a hardware-booting base fit and a hardware-failing FM fit |
| the FM RTL delta | provably audio-only and fire-and-forget (toggle handshake, no ack, no path to video/reset/bus); its real effect is a heavier fit (59 % vs 39 % ALMs) |

Key structural facts found on the way:
- The DQ read capture is a **soft DDIO in fabric** (`ddio_in_d0b:auto_generated`),
  reached through ~1.1–1.3 ns of routed interconnect; `FAST_INPUT_REGISTER ON -to
  dram_dq[*]` (and the FAST_OUTPUT lines for dram_a/ba/dq) sit in the fitter's
  **Ignored Assignments** panel in every build — era builds included. The
  "FAST_INPUT_REGISTER fixes FM boot fragility" commit worked by coincidence.
- `dram_clk` is a **combinational assign of the PLL clock to a normal output pin**
  (`pocket_soc.py`): 9.1 ns to the pin, 4.7 ns of it plain routed fabric.
- The whole SDRAM loop is **unconstrained in STA** (the DQ inputs are among the 21
  unconstrained input ports). "+2.022 ns slack" never covered the thing that fails.
- `sys_init()` halts in `for(;;)` with video DMA off when `sdram_init()` fails
  (diag `0xDEADD3A2`) — which presents on hardware exactly as the observed frozen
  black screen.

## The instrument that replaced the Pocket

Cyclone V has NO post-fit timing simulation (28 nm limitation), but functional
gate-level simulation of the EXACT failing fit works under the bundled Questa FSE:

1. `quartus_eda --simulation --tool=modelsim_oem --format=verilog` → `ap_core.vo`
   (the failing fit regenerates deterministically: sha `d6373587…` = the flashed
   FM4pin core).
2. Libraries (order matters): `altera_lnsim.sv` (-sv), `altera_primitives.v`,
   `cyclonev_atoms.v`, `mentor/cyclonev_atoms_ncrypt.v`. License:
   `SALT_LICENSE_SERVER=$HOME/.altera.quartus/questa_lic.dat`.
3. Testbench: clock `clk_74a`, a behavioral 32M×16 SDR SDRAM model on the `dram_*`
   pins, `force {\ic|icb|reset_n~q} 1` + `{\reset_n~q} 1` to stand in for the
   Pocket MCU's reset-exit command, decode the firmware's own console from the
   top-level `dbg_tx` pin (115200), count ROM-fetch toggles as a CPU-alive signal.

**VERDICT (sim completed 2026-07-25): the failing fit's logic is SOUND.** On the
failing bitstream's own netlist: the CPU fetches and executes, firmware prints
"Initializing SDRAM @0x40000000" on its console, the DFII software command path
delivers the full init sequence (LOAD MODE CL=2 BL=1), and the memtest writes
16,384 words and reads every one of them back CORRECTLY (the model's hit counter
tracked stored data exactly; zero mismatches through 14k+ verified reads before
the sim's time cutoff). A fitter miscompile is ruled out. The black screen is
therefore at-speed analog alignment of the unconstrained SDRAM loop — the only
per-fit variable left standing (the clock route; see the fix below, implemented
as `main` 1563d4c / `opl3` 1002988 with the re-centering sweep install
`RiscvStackDDIOSweep.zip`).

## The durable fix, independent of the final verdict

1. **Constrain the SDRAM interface** (source-synchronous SDC: generated clock on
   the `dram_clk` pin, `set_input_delay`/`set_output_delay` from the AS4C32M16
   window + the PHY's read-latency multicycles) so STA polices the capture window
   and a bad fit becomes a FAILED BUILD, not a black screen.
   `scratchpad` prototype: `dq_window.tcl` overlay (kept in session artifacts).
2. **Drive `dram_clk` from an IOE DDIO** instead of the fabric-routed assign —
   makes the largest fit-dependent term a silicon constant (needs a one-time phase
   re-center on hardware).
3. Keep builds deterministic (Part 1), so a hardware-validated bitstream is
   reproducible forever.

## Closure (2026-07-26)

The loop is fully closed:
- **Root causes fixed**: ISA-order pin (reproducible fits) + IOE-DDIO `dram_clk`
  (fit-independent clock path) + phase 150 both flavors (hardware-measured
  window {135..165}).
- **v1.0.0 released** with bitstreams byte-identical to hardware-verified cores.
- **The window is now under STA**: `core/sdram_window.sdc` constrains the loop
  (values tuned so the verified fits pass with real margin: capture +2.07 setup /
  +12.4 hold, cmd +1.96/+0.38 at introduction), and `tools/check_sdram_window.tcl`
  gates every `build_core.sh` run in both corners. With the constraints active the
  fitter REBALANCES the interface (holds 0.38/0.35 -> ~1.2/1.5) — base and FM come
  out with matched interface timing (within 20 ps of each other).
- The constrained fits are new bitstreams (base `4df50029…`, FM `140a77de…`,
  v1.0.1-rc1) — hardware verification via `RiscvStackSDCVerify.zip`.

The gate turns the entire failure class this document describes into a build
error. If it ever trips, something material moved — find it before flashing.
