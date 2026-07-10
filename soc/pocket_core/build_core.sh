#!/usr/bin/env bash
# Full core build + deploy. Syncs the freshly generated SoC RTL from
# soc/build/pocket/gateware (netlist hash + firmware .init files change between
# builds — hand-copying shipped stale RTL/firmware more than once), compiles with
# Quartus, bit-reverses the FRESH rbf (asserting rbf==rbf_r), drops it into the
# spc-clone package (the packaging format the Pocket actually accepts — see
# litex-pocket-gotchas), bumps version, zips, uploads.
#
# VER is REQUIRED (no default: a stale default once "downgraded" the package).
#
# SPDX-License-Identifier: BSD-2-Clause
set -euo pipefail
cd "$(dirname "$0")"

QUARTUS_SH="${QUARTUS_SH:-$HOME/altera_lite/25.1std/quartus/bin/quartus_sh}"
REVERSE="/home/carlos/devel/fpga/spc-pocket-player/tools/reverse_bits.py"
: "${VER:?set VER explicitly, e.g. VER=0.7.0 ./build_core.sh}"
GW="/home/carlos/devel/fpga/riscv-stack/soc/build/pocket/gateware"
PKG="/home/carlos/devel/fpga/riscv-stack/soc/spc_clone/out"      # working spc-clone tree
# Flavor-agnostic: the single Cores/<author>.<shortname> dir in the package tree
# IS the flavor identity (differs per branch); zip name follows the shortname.
NCORES=$(ls "$PKG/Cores" | wc -l)
[ "$NCORES" = "1" ] || { echo "FATAL: expected exactly ONE Cores/<flavor> dir, found $NCORES (merge leftover?)"; exit 1; }
CDIR="$PKG/Cores/$(ls "$PKG/Cores")"
SHORTNAME=$(python3 -c "import json;print(json.load(open('$CDIR/core.json'))['core']['metadata']['shortname'])")
UPLOAD_SH="/home/carlos/devel/mysharedbucket/upload.sh"

echo "== [0/4] sync generated RTL from $GW =="
[ -f "$GW/pocket_platform.v" ] || { echo "FATAL: no generated SoC in $GW (run ../build.sh hw phases first)"; exit 1; }
cp "$GW/pocket_platform.v" "$GW"/pocket_platform_*.init .
# The CPU netlist filename embeds a config hash; the generated qsf knows the right
# one (absolute path) plus the vendor RAM side-files. Mirror them + rewrite ours.
NET=$(sed -n 's/.*\(SYSTEMVERILOG_FILE\|VERILOG_FILE\) \(\S*VexiiRiscvLitex_[0-9a-f]*\.v\).*/\2/p' "$GW/pocket_platform.qsf" | head -1)
[ -n "$NET" ] && [ -f "$NET" ] || { echo "FATAL: CPU netlist not found via $GW/pocket_platform.qsf"; exit 1; }
NETDIR=$(dirname "$NET"); NETBASE=$(basename "$NET")
rm -f VexiiRiscvLitex_*.v
cp "$NET" .
cp "$NETDIR/Ram_1w_1rs_Intel.v" "$NETDIR/Ram_1w_1ra_Generic.v" .
sed -i "s|VexiiRiscvLitex_[0-9a-f]*\.v|$NETBASE|" ap_core.qsf
grep -q "$NETBASE" ap_core.qsf || { echo "FATAL: qsf netlist rewrite failed"; exit 1; }
echo "   CPU netlist: $NETBASE"

echo "== [1/4] Quartus compile =="
# Remove the previous bitstream so a partially-resumed flow can never package it.
rm -f output_files/ap_core.rbf
"$QUARTUS_SH" --flow compile ap_core
RBF="output_files/ap_core.rbf"
[ -f "$RBF" ] || { echo "FATAL: $RBF not produced"; exit 1; }

echo "== [2/4] reverse fresh rbf -> package (assert size) =="
python3 "$REVERSE" "$RBF" "$CDIR/bitstream.rbf_r"
a=$(stat -c %s "$RBF"); b=$(stat -c %s "$CDIR/bitstream.rbf_r")
[ "$a" = "$b" ] || { echo "FATAL: size mismatch rbf=$a rbf_r=$b"; exit 1; }
echo "   rbf == rbf_r == $a bytes (OK)"

echo "== [3/4] bump version + validate JSON =="
sed -i "s|\"version\": \"[^\"]*\"|\"version\": \"$VER\"|" "$CDIR/core.json"
grep -q "\"version\": \"$VER\"" "$CDIR/core.json" || { echo "FATAL: version bump did not apply"; exit 1; }
for j in "$CDIR"/*.json "$PKG"/Platforms/*.json; do python3 -m json.tool "$j" >/dev/null; done

echo "== [4/4] zip + upload =="
ZIP="/home/carlos/devel/fpga/riscv-stack/soc/${SHORTNAME}_v${VER}.zip"
rm -f "$ZIP"; (cd "$PKG" && zip -qr "$ZIP" Cores Platforms $( [ -d Assets ] && echo Assets ))
ls -la "$ZIP"
[ -x "$UPLOAD_SH" ] && "$UPLOAD_SH" thinkcentre.local:8000 "$ZIP" Carlos/fpga/ || echo "(upload skipped)"
echo "== done: $ZIP =="
