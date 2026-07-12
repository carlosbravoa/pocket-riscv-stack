#!/usr/bin/env python3
"""Convert Allegro 4's GM AdLib patch set (fm_instr.h) to a Creative .ibk.

    allegro_to_ibk.py fm_instr.h allegro.ibk

fm_instr.h comes from the Allegro library (giftware license), e.g.
https://raw.githubusercontent.com/liballeg/allegro5/4.4/src/misc/fm_instr.h
The active (#if 1) set is the 128-instrument GM bank by Jorrit Rouwe.

FM_INSTRUMENT field order (characteristic1/2, level1/2, attackdecay1/2,
sustainrelease1/2, wave1/2, feedback) is exactly the first 11 bytes of an
SBI/IBK record, so the conversion is a straight repack. Melodic only —
Allegro's FM driver has no GM drum patch table; the player falls back to
the default bank's percussion.
"""
import re
import sys


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src = open(sys.argv[1], encoding="utf-8", errors="replace").read()

    # keep only the active branch of the "#if 1 ... #else ... #endif",
    # after the table declaration (the struct definition also uses braces)
    body = src.split("fm_instrument[128]", 1)[1]
    body = body.split("#if 1", 1)[1].split("#else", 1)[0]

    rows = re.findall(r"\{([0-9xXa-fA-F,\s]+)\}\s*,?\s*(?:/\*\s*(.*?)\s*\*/)?",
                      body)
    rows = [r for r in rows if len(re.findall(r"0[xX][0-9a-fA-F]+|\d+",
                                              r[0])) == 14]
    if len(rows) != 128:
        sys.exit(f"expected 128 instruments, found {len(rows)}")

    out = bytearray(b"IBK\x1a")
    names = bytearray()
    for vals, name in rows:
        b = [int(x, 0) for x in
             re.findall(r"0[xX][0-9a-fA-F]+|\d+", vals)]
        rec = bytes(b[:11]) + bytes(5)          # percvoc/transpose/pitch/pad
        out += rec
        names += name.encode("ascii", "replace")[:8].ljust(9, b"\0")[:8] + b"\0"
    out += names
    assert len(out) == 4 + 128 * 16 + 128 * 9, len(out)
    open(sys.argv[2], "wb").write(out)
    print(f"{sys.argv[2]}: 128 instruments, {len(out)} bytes")


if __name__ == "__main__":
    main()
