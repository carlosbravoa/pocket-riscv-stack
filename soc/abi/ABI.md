# RISC-V Stack console — family ABI contract

This is the promise the console makes to the games built for it: **a `.bin` built
against ABI v1 runs on any v1-or-later bitstream, on every flavor of the family.**
It exists because that promise is fragile by construction and easy to break by
accident — read this before touching `pocket_soc.py`'s CSR list.

## Why the map is fragile

LiteX assigns each CSR its bus address by the **order** it is defined in
`pocket_soc.py`. Every game statically links `hal.c`, which reads/writes those
addresses from the generated `csr.h` — the offsets are **baked into the binary**.
So inserting or reordering one CSR shifts every address after it, and every
existing `.bin` then reads/writes the *wrong* registers on the next bitstream. No
crash, no warning — just silent corruption.

The lock removes that hazard.

## The lock

- **`abi_v1_csr_map.txt`** — the golden map: the 90 CSRs shipped in **v0.24.0**,
  name → address. This is ABI **v1**, frozen.
- **`check_abi.py`** — verifies a generated `csr.h` against the golden. Every
  locked address must still be present at its locked offset. It runs in the sim
  gate (`run_sim.sh`) so a bad edit fails loudly.

## The rules

1. **Append only.** New CSRs go at the **end** of the main-region CSR list in
   `pocket_soc.py` (after `abi_version`). Appending is ABI-safe: old bins never
   touch the new addresses; new bins detect the new capability via `hwfeat`.
2. **Never move or remove a locked CSR.** If you ever truly must (you almost
   never must — appending a replacement is nearly always the answer), it is a
   **breaking change**: bump the ABI **major**, re-snapshot the golden, and
   accept that every existing `.bin` must be rebuilt.
3. **Bump the version on append.** Appending is backward-compatible → bump the
   **minor** in `ABI_VERSION` (`pocket_soc.py`). Reserve major bumps for rule 2.
4. **The family is one map.** Both flavors (base + FM) are generated from the
   *same* `pocket_soc.py`; only `core_top.v` + the `.qsf` + package identity
   differ. There must be **zero** CSR-list differences between branches
   (`git diff main opl3 -- soc/pocket_soc.py` on the CSR lines = empty).
5. **Capabilities are feature bits, not versions.** A game checks
   `sys_caps()->features & HAL_FEAT_*` to use an optional block (blitter,
   colorkey, FM…), never the version number. `hwfeat` is driven per-flavor by
   `core_top`; `abi_version` is just the coarse compatibility stamp.

## Adding a CSR (the safe procedure)

```sh
# 1. Add `self.my_new = CSR...` as the LAST main-region CSR in pocket_soc.py,
#    below abi_version. Bump ABI_VERSION's minor.
# 2. Regenerate headers (no Quartus):
cd soc && python pocket_soc.py --output-dir build/pocket           # + --simcore, --sim
# 3. Guard must still pass (locked map intact, your CSR appended):
python3 abi/check_abi.py --check build/pocket/software/include/generated/csr.h abi/abi_v1_csr_map.txt
# 4. Add a HAL accessor + a HAL_FEAT_* bit if it's an optional block; gate the
#    game code on sys_caps()->features.
# 5. When the bitstream that includes it SHIPS, re-snapshot the golden so the new
#    CSR's address is locked too:
python3 abi/check_abi.py --snapshot <that csr.h> abi/abi_v1_csr_map.txt
```

## Version register

`abi_version` (read-only CSR, `main_abi_version`) is hardwired to `ABI_VERSION`
(`major<<16 | minor`, currently `0x0001_0000` = v1.0). The HAL exposes it as
`sys_abi_version()`, and `HAL_ABI_VERSION` is what a given build targets. A
pre-lock bitstream (before v1) lacks the register and reads `0`.

## Status

- **v1.0** = the v0.24.0 CSR map (90 CSRs) + the `abi_version` stamp appended.
- Locked and guarded as of `soc/abi/` (this dir). Activates in hardware on the
  first bitstream built after the lock; existing v0.24.0 binaries are unaffected.
