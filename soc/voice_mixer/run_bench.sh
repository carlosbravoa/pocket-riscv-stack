#!/usr/bin/env bash
# Voice mixer P1 bench: regenerate reference vectors, Verilate, run, PASS/FAIL.
# SPDX-License-Identifier: BSD-2-Clause
set -euo pipefail
cd "$(dirname "$0")"

python3 gen_ref.py

verilator --binary --timing -j "$(nproc)" \
    -Wno-WIDTH -Wno-UNUSEDSIGNAL -Wno-UNUSEDPARAM -Wno-CASEINCOMPLETE \
    -Wno-BLKSEQ -Wno-INITIALDLY \
    --top-module voice_mixer_tb voice_mixer.v voice_mixer_tb.v

./obj_dir/Vvoice_mixer_tb | tee bench.log
grep -q "RESULT: PASS" bench.log
