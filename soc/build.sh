#!/usr/bin/env bash
# Build the Stage-1 SoC + firmware for a target.
#
#   ./build.sh sim   Verilator simulation (boots firmware, prints over UART)
#   ./build.sh hw    Quartus synthesis for the Pocket Cyclone V (bitstream + fit)
#
# Two-phase: (1) elaborate the SoC to emit generated/ headers, (2) build the
# firmware against them, (3) rebuild the SoC with the firmware baked into ROM
# and run the toolchain.
#
# SPDX-License-Identifier: BSD-2-Clause
set -euo pipefail

TARGET="${1:-sim}"
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

# Push the built firmware ELF to the shared bucket (non-fatal if unreachable).
# Override host/dest with UPLOAD_HOST / UPLOAD_DEST; skip entirely with UPLOAD=0.
UPLOAD="${UPLOAD:-1}"
UPLOAD_HOST="${UPLOAD_HOST:-thinkcentre.local:8000}"
UPLOAD_DEST="${UPLOAD_DEST:-Carlos/fpga/}"
UPLOAD_SH="/home/carlos/devel/mysharedbucket/upload.sh"
upload_elf() {
  [ "$UPLOAD" = "1" ] || return 0
  [ -x "$UPLOAD_SH" ] && [ -f "$1" ] || return 0
  echo "== uploading $(basename "$1") -> $UPLOAD_HOST $UPLOAD_DEST =="
  "$UPLOAD_SH" "$UPLOAD_HOST" "$1" "$UPLOAD_DEST" || echo "  (upload skipped: server unreachable)"
}

case "$TARGET" in
  sim) SOC_FLAGS="--sim"; OUT="build/sim" ;;
  hw)  SOC_FLAGS="";      OUT="build/pocket" ;;
  *)   echo "usage: $0 {sim|hw}"; exit 1 ;;
esac

echo "== [1/3] elaborate SoC ($TARGET) -> generated headers =="
python pocket_soc.py $SOC_FLAGS --output-dir "$OUT"

echo "== [2/3] build firmware against $OUT =="
make -C firmware clean
make -C firmware BUILD_DIR="$HERE/$OUT"
FW="$HERE/firmware/firmware.bin"
ls -l "$FW"
upload_elf "$HERE/firmware/firmware.elf"

echo "== [3/3] rebuild SoC with firmware in ROM + run toolchain =="
python pocket_soc.py $SOC_FLAGS --build --firmware "$FW" --output-dir "$OUT"

echo "== done ($TARGET) =="
