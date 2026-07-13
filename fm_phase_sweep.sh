#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/soc"
SOC="$PWD"
for P in 160 180 200; do
  echo "==== FM sweep: 66 MHz, phase ${P} ===="
  RVSTACK_DRAM_PHASE=$P python pocket_soc.py --output-dir build/pocket >/dev/null
  make -s -C firmware BUILD_DIR="$SOC/build/pocket" >/dev/null
  RVSTACK_DRAM_PHASE=$P python pocket_soc.py --firmware firmware/firmware.bin --output-dir build/pocket >/dev/null
  ( cd pocket_core && rm -rf db incremental_db qdb output_files
    VER="0.FMSWEEP-p${P}" ./build_core.sh >/dev/null 2>&1 )
  echo "  -> done phase ${P}"
done
echo "==== FM sweep done ===="
