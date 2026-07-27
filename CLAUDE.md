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
Latest release: **v1.1.0** (2026-07-27) — the 32-voice hardware sample mixer
(ABI 1.1, both flavors) + the 71-minute timebase-wrap fix in game bins.
Released bitstreams are byte-identical to hardware-verified cores; the vmx
CSRs are LOCKED in the golden map. DRAM phase = 150 both flavors (DDIO clock
path). Voice-mixer story: soc/AUDIO_VOICE_MIXER_SCOPING.md (P0-P4 done; P5 =
tracker-music games). Pak auto-load ROOT-CAUSED 2026-07 (paktest hardware
probe): openfile resolves from the SD ROOT only — `pak_bind_named()` now
prefixes `/Assets/riscv_stack/common/`; sim TB models SD-root semantics.
Open: hardware confirm of the fixed auto-load (new tyrian.bin/paktest.bin
on the bucket — Tyrian should boot with no manual Pak pick).
2026-07-27: v0.20-era bins (doom/wolf3d/keen/quabricks/midiplay) rebuilt —
they'd been silently broken since a pre-lock CSR shift (see the
`v020-bins-abi-shift` memory); port branches resynced to the v1.1 SDK.
SkyRoads ported from Skyroads-c (`sdk/skyroads/`, replaces the prototype):
indexed renderer proven pixel-exact upstream, MUZAX on real OPL3, C++17/STL
console lane documented in its Makefile. Gates 1-3 green; hardware pending
(skyroads.bin/.pak on the bucket).
