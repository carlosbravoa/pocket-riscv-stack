#!/usr/bin/env python3
"""Pre-convert SDLPoP's loose indexed-PNG assets to a decode-free raw format.

    png2raw.py <src-data-dir> <dst-staging-dir>

Runtime lodepng PNG decode is ~1.17M cycles per glyph on the 74 MHz rv32 —
far too slow for the console (255 font images ~= 300M cycles; thousands of
sprites). Instead we decode the PNGs on the HOST at pak-build time into a tiny
raw indexed blob (compat/pop_png.c reads it with a memcpy, no decoder):

    "RVI1" | u16 w | u16 h | u16 ncolors | ncolors*[r,g,b] | w*h * index

The blob keeps the SAME filename (resNNN.png) so the game's load path is
unchanged; pop_png.c sniffs the "RVI1" magic and takes the raw path, falling
back to lodepng for anything else (loose-dir PC-twin dev). Non-PNG files
(.pal/.bin/.txt) are copied verbatim. The PC twin reads the same pak.

SPDX-License-Identifier: BSD-2-Clause
"""
import os
import struct
import sys
import shutil

try:
    from PIL import Image
except ImportError:
    sys.exit("png2raw: needs Pillow (pip install Pillow)")


def convert_png(src, dst):
    im = Image.open(src)
    if im.mode != "P":                      # not palette-indexed: leave as PNG
        shutil.copy(src, dst)               # (pop_png.c lodepng handles it)
        return False
    w, h = im.size
    idx = im.tobytes()                      # PIL expands sub-byte to 8bpp
    if len(idx) != w * h:
        shutil.copy(src, dst)
        return False
    pal = im.getpalette() or []
    ncolors = min(len(pal) // 3, 256)
    hdr = b"RVI1" + struct.pack("<HHH", w, h, ncolors)
    with open(dst, "wb") as f:
        f.write(hdr)
        f.write(bytes(pal[:ncolors * 3]))
        f.write(idx)
    return True


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src_root, dst_root = sys.argv[1], sys.argv[2]
    n_png = n_copy = 0
    for dirpath, _dirs, files in os.walk(src_root):
        rel = os.path.relpath(dirpath, src_root)
        out_dir = os.path.join(dst_root, rel) if rel != "." else dst_root
        os.makedirs(out_dir, exist_ok=True)
        for name in files:
            src = os.path.join(dirpath, name)
            dst = os.path.join(out_dir, name)
            if name.lower().endswith(".png") and convert_png(src, dst):
                n_png += 1
            else:
                shutil.copy(src, dst)
                n_copy += 1
    print(f"png2raw: {n_png} PNGs -> raw, {n_copy} copied -> {dst_root}")


if __name__ == "__main__":
    main()
