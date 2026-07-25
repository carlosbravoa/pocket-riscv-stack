#!/usr/bin/env bash
# Build ONE Pocket install containing several FM cores side by side, so a boot
# failure can be bisected in a single flash.
#
# Packaging rule (CLAUDE.md): one Cores/<author>.<shortname>/ folder PER bitstream,
# ALL sharing ONE platform_ids -> ONE Platforms/<id>.json + ONE Assets/<id>/common.
# NOT one platform per bitstream (that bloats the Pocket library) and NOT a
# variants.json toggle (Carlos wants visible separate core folders).
#
# The ladder this builds — each step changes exactly ONE thing:
#   A pub24      published v0.24.0 bitstream + published v0.24.0 packaging
#   B repack24   published v0.24.0 bitstream + OUR packaging          (A vs B = packaging)
#   C rebuild24  OUR rebuild of fm-v0.24.0   + OUR packaging          (B vs C = bitstream)
#   D fm100      FM 1.0 candidate (opl3 HEAD)+ OUR packaging          (C vs D = source delta)
#
# Assets and the platform are held CONSTANT (taken from the published release) —
# they are shared by every core in one install, so they cannot be a variable here.
#
# usage: soc/tools/make_ab_test_zip.sh <published-family.zip> <our-v0.24-FM.zip> <our-1.0-FM.zip> <out.zip>
# SPDX-License-Identifier: BSD-2-Clause
set -euo pipefail

PUBFAM="${1:?published RiscvStackFamily_v0.24.0.zip}"
OUR024="${2:?our RiscvStackFM_v0.24.0.zip}"
OUR100="${3:?our RiscvStackFM_v1.0.0.zip}"
OUT="${4:?output zip path}"

T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
mkdir -p "$T/pub" "$T/o24" "$T/o10" "$T/stage/Cores" "$T/stage/Saves/riscv_stack"
unzip -qo "$PUBFAM" -d "$T/pub"
unzip -qo "$OUR024" -d "$T/o24"
unzip -qo "$OUR100" -d "$T/o10"

PUBCORE="$T/pub/Cores/bravo.RiscvStackFM"
O24CORE="$T/o24/Cores/bravo.RiscvStackFM"
O10CORE="$T/o10/Cores/bravo.RiscvStackFM"
for d in "$PUBCORE" "$O24CORE" "$O10CORE"; do
    [ -f "$d/bitstream.rbf_r" ] || { echo "FATAL: no bitstream in $d"; exit 1; }
done

# Platform + assets: constant, from the published release (known-good, ABI-matched).
cp -r "$T/pub/Platforms" "$T/stage/"
cp -r "$T/pub/Assets"    "$T/stage/"
printf 'Per-game save files (<game>.sav) are created here on demand.\n' \
    > "$T/stage/Saves/riscv_stack/readme.txt"

# add_core <shortname> <src-core-dir-for-json> <bitstream-file> <description>
add_core() {
    local sn="$1" jsrc="$2" bit="$3" desc="$4"
    local dst="$T/stage/Cores/bravo.$sn"
    mkdir -p "$dst"
    # info.txt has a structured title/blank-line/body format and is copied VERBATIM:
    # this is a controlled experiment, so nothing that isn't the variable changes.
    cp "$jsrc"/*.json "$jsrc"/icon.bin "$jsrc"/info.txt "$dst"/
    cp "$bit" "$dst/bitstream.rbf_r"
    python3 - "$dst/core.json" "$sn" "$desc" <<'PY'
import json, sys
path, sn, desc = sys.argv[1:4]
d = json.load(open(path))
m = d["core"]["metadata"]
m["shortname"]    = sn
m["platform_ids"] = ["riscv_stack"]     # ONE shared platform for every test core
# The shipped v0.24.0 core.json carries a 101-char description, so that length is
# known-accepted by the Pocket. Stay at or under it -- a rejected core.json would
# look exactly like "the core doesn't boot" and poison the experiment.
assert len(desc) <= 101, f"description too long ({len(desc)}): {desc}"
assert len(sn)   <= 31,  f"shortname too long ({len(sn)}): {sn}"
m["description"]  = desc
json.dump(d, open(path, "w"), indent=2)
PY
    echo "   + bravo.$sn  ($(stat -c %s "$dst/bitstream.rbf_r") B)"
}

echo "== staging A/B cores =="
add_core FMApub24     "$PUBCORE" "$PUBCORE/bitstream.rbf_r" \
    "A CONTROL: the released v0.24.0 bitstream with its own packaging. This one is known to work."
add_core FMBrepack24  "$O24CORE" "$PUBCORE/bitstream.rbf_r" \
    "B: released v0.24.0 bitstream, REBUILT packaging. A boots and B does not => packaging."
add_core FMCrebuild24 "$O24CORE" "$O24CORE/bitstream.rbf_r" \
    "C: our rebuild of fm-v0.24.0, same fit as A. B boots and C does not => the build stamps."
add_core FMD100       "$O10CORE" "$O10CORE/bitstream.rbf_r" \
    "D: FM 1.0 from opl3 HEAD, deterministic. C boots and D does not => the 1.0 source delta."

for j in "$T"/stage/Cores/*/*.json "$T"/stage/Platforms/*.json; do python3 -m json.tool "$j" >/dev/null; done
rm -f "$OUT"; (cd "$T/stage" && zip -qr "$OUT" Cores Platforms Assets Saves)
ls -la "$OUT"
echo "== unzip at the SD root; four cores appear under the one RISC-V Stack platform =="
