# Building & releasing the RISC-V Stack family — the canonical process

This is the authoritative build recipe. It has been reconstructed from the
scripts + git history after a session lost it to context compaction and
deviated (see "What NOT to do"). **Keep it current; do not rebuild from memory.**

Two flavors, one family zip:
- **base** `RiscvStack` — on branch **`main`**, DRAM capture **phase 150**.
- **FM** `RiscvStackFM` — on branch **`opl3`**, DRAM capture **phase 210**, adds
  OPL3 + the FIR resampler in `core_top.v`.

They share `soc/pocket_soc.py` (the SoC/CSR map is identical — the family ABI).
Only `core_top.v` + `ap_core.qsf` + the package identity + the phase differ.

## Prerequisites
```sh
cd /home/carlos/devel/fpga/riscv-stack && . ./env.sh   # venv + riscv-gcc + Quartus 25.1std
```

## The key constraint that dictates the process
`soc/pocket_core/build_core.sh` reads gateware from a **hardcoded** path:
`GW=/home/carlos/devel/fpga/riscv-stack/soc/build/pocket/gateware`. So whatever
flavor you want in the bitstream must be elaborated into **that** `build/pocket`.
That means **each flavor is built by checking out its branch in the MAIN working
directory** — NOT in a git worktree (a worktree has its own `build/pocket`, which
the hardcoded path ignores). This is why FM releases are "FM build sync" commits
on `opl3`: you switch the main tree to `opl3`, build, and commit the result.

## Base flavor (branch `main`, phase 150)
```sh
cd soc
git checkout main
rm -rf build/pocket pocket_core/{db,incremental_db,qdb,output_files}   # clean fit
python pocket_soc.py --output-dir build/pocket                        # elaborate (phase 150 default)
make -C firmware BUILD_DIR="$PWD/build/pocket"                         # bootloader ROM
python pocket_soc.py --firmware firmware/firmware.bin --output-dir build/pocket
python3 abi/check_abi.py --check build/pocket/software/include/generated/csr.h abi/abi_v1_csr_map.txt
cd pocket_core && VER=x.y.z ./build_core.sh                            # Quartus + package -> ../RiscvStack_vx.y.z.zip
```
Verify: `CLK1_PHASE_SHIFT (13'd5611)` (=150° @74.25) in the generated
`pocket_platform.v`; `rbf == rbf_r`; `core.json` version bumped.

## FM flavor (branch `opl3`, phase 210)
Commit/stash any `main` work first (this switches the main tree's branch).
```sh
cd <repo root>
git stash -u   # or commit; the main tree must be clean to switch branches
cd soc
git checkout opl3
rm -rf build/pocket pocket_core/{db,incremental_db,qdb,output_files}
python pocket_soc.py --output-dir build/pocket                        # phase 210 = opl3 default
make -C firmware BUILD_DIR="$PWD/build/pocket"
python pocket_soc.py --firmware firmware/firmware.bin --output-dir build/pocket
cd pocket_core && VER=x.y.z ./build_core.sh                           # -> ../RiscvStackFM_vx.y.z.zip
git commit -am "FM build sync for vx.y.z"                             # capture the FM build state on opl3
cd .. && git checkout main && cd .. && git stash pop                 # restore main
```
Verify: `CLK1_PHASE_SHIFT (13'd7856)` (=210° @74.25). **FM is fit-sensitive on
the SDRAM read — read `pocket_core/FM_BUILD_NOTES.md` before trusting a rebuild.**

## Family zip (the ONLY distributed artifact)
```sh
cd soc && VER=x.y.z ./tools/make_family_zip.sh   # merges both per-flavor zips -> RiscvStackFamily_vx.y.z.zip + uploads to the bucket
```
Both `RiscvStack_vx.y.z.zip` and `RiscvStackFM_vx.y.z.zip` must exist in `soc/`.

## GitHub release (milestones only — policy: family zip only)
```sh
~/tools/gh_2.86.0_linux_amd64/bin/gh release create vx.y.z \
  --repo carlosbravoa/pocket-riscv-stack \
  --title "vx.y.z — <headline>" --notes-file RELEASE_NOTES_vx.y.z.md \
  soc/RiscvStackFamily_vx.y.z.zip
```

## What NOT to do (the 1.0 FM failure, 2026-07-15)
- **Do NOT build a flavor from a git worktree** with a patched `build_core.sh`.
  The proven flow uses the main tree on the flavor's branch. The worktree build
  produced FM bitstreams that hung / showed DRAM garbage while the byte-identical
  source built the proven way (v0.24.0) was rock solid. Root cause never fully
  isolated remotely; the safe rule is: **build the proven way.**
- Do NOT skip the clean-tree step (stale netlist/init mix corrupts the fit).
- Do NOT change `RVSTACK_DRAM_PHASE` between flavor builds (shells are fresh, so
  it won't leak — but never export it).
