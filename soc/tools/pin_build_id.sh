#!/usr/bin/env bash
# Pin (or restore) the APF build-id MIF so the bitstream is byte-reproducible.
#
# apf/build_id_gen.tcl is a Quartus PRE_FLOW_SCRIPT_FILE (ap_core.qsf:47). It rewrites
# apf/build_id.mif on EVERY compile with the date, the time and
# buildUnique = int(rand()*4294967295). That MIF initialises the mf_datatable M10K
# (core/core_bridge_cmd.v:565), so the nonce ends up inside the bitstream and no two
# builds are ever byte-identical. Analogue documents this exact switch at lines
# 162-171 of their own script.
#
#   soc/tools/pin_build_id.sh pin [DATE] [TIME] [UNIQ]
#   soc/tools/pin_build_id.sh restore
#
# See soc/REPRODUCIBILITY.md.
# SPDX-License-Identifier: BSD-2-Clause
set -euo pipefail
cd "$(dirname "$0")/../pocket_core"

TCL=apf/build_id_gen.tcl
MIF=apf/build_id.mif

case "${1:-}" in
pin)
    DATE="${2:-20260101}"; TIME="${3:-00000000}"; UNIQ="${4:-00000000}"
    sed -i 's/^generateBuildID_MIF$/# generateBuildID_MIF   # PINNED (soc\/tools\/pin_build_id.sh)/' "$TCL"
    cat > "$MIF" <<MIFEOF
-- Build ID Memory Initialization File
--

DEPTH = 256;
WIDTH = 32;
ADDRESS_RADIX = HEX;
DATA_RADIX = HEX;

CONTENT
BEGIN

   0E0 : $DATE;
   0E1 : $TIME;
   0E2 : $UNIQ;

END;
MIFEOF
    grep -q '^# generateBuildID_MIF' "$TCL" || { echo "FATAL: could not disable the generator"; exit 1; }
    echo "build id PINNED to $DATE / $TIME / $UNIQ"
    ;;
restore)
    sed -i 's/^# generateBuildID_MIF.*$/generateBuildID_MIF/' "$TCL"
    grep -q '^generateBuildID_MIF$' "$TCL" || { echo "FATAL: could not re-enable the generator"; exit 1; }
    echo "build id generator RESTORED (bitstreams are no longer byte-reproducible)"
    ;;
*)
    sed -n '2,20p' "$0"; exit 1 ;;
esac
