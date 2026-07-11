#!/usr/bin/env bash
# Merge both flavor zips into ONE install: unzip RiscvStack_v$VER.zip +
# RiscvStackFM_v$VER.zip (they share Platforms/ and Assets/ byte-identically,
# each brings its own Cores/ dir) into RiscvStackFamily_v$VER.zip.
# Usage: VER=x.y.z ./make_family_zip.sh   (run after building both flavors)
# SPDX-License-Identifier: BSD-2-Clause
set -euo pipefail
: "${VER:?set VER}"
SOC="$(cd "$(dirname "$0")/.." && pwd)"
A="$SOC/RiscvStack_v${VER}.zip"; B="$SOC/RiscvStackFM_v${VER}.zip"
[ -f "$A" ] && [ -f "$B" ] || { echo "FATAL: build both flavors first ($A / $B)"; exit 1; }
T=$(mktemp -d)
unzip -qo "$A" -d "$T"
unzip -qo "$B" -d "$T"
[ -d "$T/Cores/bravo.RiscvStack" ] && [ -d "$T/Cores/bravo.RiscvStackFM" ] \
  || { echo "FATAL: expected both core dirs in the merge"; exit 1; }
# Per-game saves land in Saves/riscv_stack/. The Pocket's openfile creates
# FILES on demand but not parent DIRECTORIES (hardware: result 3 "not found"
# with create-if-missing set) -- ship the directory. The readme also keeps zip
# tools that skip empty dirs from dropping it.
mkdir -p "$T/Saves/riscv_stack"
printf 'Per-game save files (<game>.sav) are created here on demand.\n' \
  > "$T/Saves/riscv_stack/readme.txt"
OUT="$SOC/RiscvStackFamily_v${VER}.zip"
rm -f "$OUT"; (cd "$T" && zip -qr "$OUT" Cores Platforms Assets Saves)
rm -rf "$T"; ls -la "$OUT"
UP="/home/carlos/devel/mysharedbucket/upload.sh"
[ -x "$UP" ] && "$UP" thinkcentre.local:8000 "$OUT" Carlos/fpga/ || echo "(upload skipped)"
