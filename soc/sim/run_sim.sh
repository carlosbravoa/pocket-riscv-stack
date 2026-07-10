#!/usr/bin/env bash
# Full-system simulation: REAL core_top + REAL SoC (simcore variant: model
# DRAM inside) + scripted Pocket-host testbench. See tb_core_top.cpp.
#
#   ./run_sim.sh            build everything + run the save/exit scenario
#   ./run_sim.sh --trace    also dump core_top.vcd (big, slow)
#   SKIP_SOC=1 ./run_sim.sh reuse build/simcore (RTL/firmware unchanged)
#
# SPDX-License-Identifier: BSD-2-Clause
set -euo pipefail
cd "$(dirname "$0")"
SOC=..
PC=$SOC/pocket_core
GW=$SOC/build/simcore/gateware

if [ "${SKIP_SOC:-0}" != "1" ]; then
  echo "== [1/4] simcore SoC + firmware =="
  (cd $SOC && python pocket_soc.py --simcore --output-dir build/simcore)
  (cd $SOC && make -C firmware clean >/dev/null && make -C firmware BUILD_DIR="$PWD/build/simcore")
  (cd $SOC && python pocket_soc.py --simcore --firmware firmware/firmware.bin --output-dir build/simcore)
fi

echo "== [2/4] savetest game (against simcore headers) =="
make -C $SOC/../sdk/savetest BUILD_DIR="$(cd $SOC/build/simcore && pwd)" clean >/dev/null
make -C $SOC/../sdk/savetest BUILD_DIR="$(cd $SOC/build/simcore && pwd)"

echo "== [3/4] verilate =="
NET=$(sed -n 's/.*\(SYSTEMVERILOG_FILE\|VERILOG_FILE\) \(\S*VexiiRiscvLitex_[0-9a-f]*\.v\).*/\2/p' "$GW/pocket_platform.qsf" | head -1)
NETDIR=$(dirname "$NET")
verilator --cc --exe --build -j "$(nproc)" \
  --top-module core_top -DSIM \
  -Wno-fatal -Wno-WIDTH -Wno-PINMISSING -Wno-UNOPTFLAT -Wno-TIMESCALEMOD \
  -Wno-CASEINCOMPLETE -Wno-INITIALDLY -Wno-BLKANDNBLK -Wno-MULTIDRIVEN \
  --trace \
  -CFLAGS "-std=c++17 -O2 -g" \
  -y "$GW" -y "$NETDIR" -y "$PC/core" -y "$PC/apf" -y . \
  +libext+.v+.sv \
  sim_config.vlt \
  sim_support.v \
  "$NETDIR/Ram_1w_1ra_Generic.v" \
  "$NETDIR/Ram_1w_1rs_Generic.v" \
  "$PC/apf/common.v" \
  "$PC/core/core_top.v" \
  "$GW/pocket_platform.v" \
  tb_core_top.cpp \
  -o sim_core_top

echo "== [4/4] run =="
# the SoC's ROM/palette .init files are read relative to CWD
cp "$GW"/*.init obj_dir/ 2>/dev/null || true
ulimit -s unlimited 2>/dev/null || true   # big verilated eval chains blow 8MB stacks
cd obj_dir && ./sim_core_top --game ../../../sdk/savetest/savetest.bin "$@"
