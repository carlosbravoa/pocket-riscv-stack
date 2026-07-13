#!/usr/bin/env bash
# SDRAM read-capture phase sweep (PocketDoom method) — hardware-in-the-loop.
# Builds the BASE flavor at a target sys clock across several dram-clock phases;
# each produces a distinctly-named family zip. Flash each, see which boots pong
# stably. 180deg is the control (known to work at 66, fail at 74.25).
#
#   RVSTACK_SYS_MHZ=74.25 ./phase_sweep.sh 200 230 260
#
# SPDX-License-Identifier: BSD-2-Clause
set -euo pipefail
cd "$(dirname "$0")/.."                       # -> soc/
SOC="$PWD"
MHZ="${RVSTACK_SYS_MHZ:-74.25}"
PHASES=("$@"); [ ${#PHASES[@]} -gt 0 ] || PHASES=(200 230 260)

for P in "${PHASES[@]}"; do
    echo "==== sweep: ${MHZ} MHz, dram phase ${P}deg ===="
    export RVSTACK_SYS_MHZ="$MHZ" RVSTACK_DRAM_PHASE="$P"
    python pocket_soc.py --output-dir build/pocket >/dev/null
    make -s -C firmware BUILD_DIR="$SOC/build/pocket" >/dev/null
    python pocket_soc.py --firmware firmware/firmware.bin --output-dir build/pocket >/dev/null
    ( cd pocket_core && rm -rf db incremental_db qdb output_files
      VER="0.SWEEP-${MHZ%%.*}M-p${P}" ./build_core.sh >/dev/null 2>&1 )
    echo "  -> RiscvStack_v0.SWEEP-${MHZ%%.*}M-p${P}.zip"
done
echo "==== sweep done: ${PHASES[*]} ===="
