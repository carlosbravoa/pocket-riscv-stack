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
