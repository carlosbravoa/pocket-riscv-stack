# RISC-V Stack — project instructions (read first)

A from-scratch Analogue Pocket game console: a LiteX SoC around a VexiiRiscv
(rv32im, no FPU) CPU at **74.25 MHz**, games as portable C `.bin` files loaded
from SD into DRAM. Two flavors from two branches (identical SoC/CSR map; differ
only in `core_top.v` + `.qsf` + package identity + DRAM phase):
- **base** `RiscvStack` — branch **`main`**, DRAM capture **phase 150**.
- **FM** `RiscvStackFM` — branch **`opl3`**, DRAM capture **phase 210**, adds OPL3 + FIR.

## Building & releasing — DO NOT reconstruct from memory
**The canonical build process is `soc/BUILD.md`. Follow it exactly.** Summary:
- Each flavor is built by checking out its branch **in the main working tree**
  (NOT a git worktree — `build_core.sh` reads a hardcoded `soc/build/pocket`).
- Per flavor: clean tree → `pocket_soc.py` elaborate → build firmware →
  re-elaborate with firmware → `cd pocket_core && VER=x.y.z ./build_core.sh`.
- Then `VER=x.y.z ./soc/tools/make_family_zip.sh` → the ONLY distributed artifact.
- GitHub releases at milestones only; family zip only.

**FM is hardware-fragile on the SDRAM read — hardware-test every FM bitstream.**
An FM bitstream can compile clean (and meet timing) yet hang/garbage on silicon.

**FM 1.0 regression — ROOT-CAUSED 2026-07-25. See `soc/REPRODUCIBILITY.md`.**
The build had three hidden random inputs, so no two builds of identical source were
ever the same build. The decisive one: LiteX md5s a **randomly-ordered Python `set`**
(the CPU ISA list) into the CPU's Verilog **module name**, so every elaboration
renames the CPU and Quartus re-places the whole design. FM is marginal, so which fit
you land on decides whether it boots; base has margin and always worked.
Pinning that order (+ `SOURCE_DATE_EPOCH`) reproduces the v0.24.0 fit exactly —
verified: the FM rebuild closes at **2.022 ns**, the figure recorded in the
`fm-v0.24.0` commit message, and shares a 1,185,121-byte unbroken run with the
shipped bitstream. Two pinned builds are byte-identical.
FALSIFIED and not worth re-testing: DRAM capture phase, Quartus drift, the
VexiiRiscv netlist cache (the regenerated netlist is byte-identical to v0.24.0's),
and the `abi_version` CSR. `FM_BUILD_NOTES.md` predates all of this.

### Multi-bitstream test cores — MULTIPLE CORE FOLDERS, one shared platform
To hardware-test several bitstreams (phase/config sweep, A/B of a suspect commit)
in ONE install: make one `Cores/<author>.<shortname>/` folder PER bitstream (each
with its own `bitstream.rbf_r`), ALL sharing ONE `platform_ids` → ONE
`Platforms/<id>.json` + ONE `Assets/<id>/common`. The Pocket lists them as separate
cores under the single platform, distinguished by `shortname` — exactly how base
`RiscvStack` and FM `RiscvStackFM` coexist under `riscv_stack`. Give each a
distinct, readable `shortname`. Repackaging only (no rebuild): each folder needs
all the JSONs + `bitstream.rbf_r`; keep `variants.json` empty.
Do NOT: (a) one platform per bitstream — copies assets+images, bloats the Pocket
library/boot, clutters the device (wrong call 2026-07-22); (b) one folder with a
`variants.json`/`cores[]` internal variant switch — Carlos wants VISIBLE separate
core folders under the platform, not a hidden variant toggle (clarified 2026-07-23).

## Family ABI is LOCKED (v1)
`soc/abi/` holds the golden CSR map + `check_abi.py` (runs in `run_sim.sh` /
`build.sh`). New CSRs may only be **appended**; never reorder/remove (breaks every
existing `.bin`). See `soc/abi/ABI.md`.

## SDK (writing games)
Everything goes through `soc/hal/hal.h`. Guides: `sdk/GUIDE.md`,
`sdk/ACCEL.md` (2D blitter / performance), `sdk/PORTABILITY.md`. Port games with
the `port-game` skill. sdl_lite (SDL1.2) / sdl2_lite (SDL2) shims for ports.

## Verify before hardware
1. PC twin (`sdk/pc/`). 2. Full-system sim (`soc/sim/run_sim.sh`, real RTL under
Verilator). Sim canNOT catch SDRAM/analog timing — only logic.

## Toolchain / infra
- `. ./env.sh` (venv + riscv-none-elf-gcc + Quartus 25.1std).
- Quartus: `~/altera_lite/25.1std/quartus/bin/quartus_sh`.
- gh: `~/tools/gh_2.86.0_linux_amd64/bin/gh`, repo `carlosbravoa/pocket-riscv-stack`.
- File bucket (LAN): `/home/carlos/devel/mysharedbucket/upload.sh thinkcentre.local:8000 <file> Carlos/fpga/`.
- Note: `.gitignore` blanket-ignores `CLAUDE.md` — this file was force-added; keep it tracked (`git add -f CLAUDE.md`).

## Status
Latest release: **v1.1.2** (2026-07-29) — SkyRoads + the bugs it flushed out.
Both flavors ABI **1.2** (append-only; every existing `.bin` still runs).
Since v1.1.0: SkyRoads ported from Skyroads-c (`sdk/skyroads/`; indexed
renderer, DOS-budget incremental draw, decode-once TREKDAT, integer physics —
each reusable by other ports); pak auto-load fixed (APF openfile resolves from
the SD ROOT only); the five v0.20-era bins rebuilt after the pre-lock CSR shift
(see the `v020-bins-abi-shift` memory) with their port branches re-synced; vmx
hardening (2-slot parity line cache, pending-tick latch, fill watchdog) plus
debug CSRs exposing the interpolator's inputs/output; FM "Exit to Menu"
un-grayed (an unmirrored fix — see BUILD.md).

**OPEN: the vmx renders FRACTIONAL playback rates wrong on SILICON** (48 kHz
exactly is perfect; RTL sim, STA and DSP mapping are all clean). Games dodge it
by feeding 48 kHz — SkyRoads converts at load. `sdk/sfxtest` is the A/B,
`sdk/vmxprobe` (PROBE2/PROBE3) the instrumented probes, and the new vmx dbg
CSRs report s0/s1/frac/out so the arithmetic can be checked numerically on
hardware. See the `voice-mixer-progress` memory for what is ruled out.

**Testing discipline learned the hard way here:** square-wave test tones hide
interpolation bugs (s1 == s0), and comparing a sim capture against ANOTHER
capture instead of the expected waveform hid a clean sim for a whole bitstream
cycle. Use real audio, compare against ground truth, and ask which core a
hardware result came from — Carlos tests on **FM**.
