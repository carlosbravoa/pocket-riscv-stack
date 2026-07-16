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

**FM is timing-fragile on the SDRAM read — read `soc/pocket_core/FM_BUILD_NOTES.md`
before rebuilding FM.** An FM bitstream can compile clean and still hang/garbage;
it must be hardware-tested and is not guaranteed to reproduce across fits. The
1.0 FM failure (2026-07-15) came from building FM the wrong way (a worktree with
a patched `build_core.sh`) — build the proven way instead.

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
Latest release: **v0.24.0** (both flavors, rock solid). **1.0 is in progress and
HELD**: base 1.0 built + hardware-verified; FM 1.0 blocked on the rebuild issue
above. Do not cut the 1.0 GitHub release until FM is genuinely clean on hardware.
