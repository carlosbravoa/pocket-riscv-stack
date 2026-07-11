#!/usr/bin/env python3
"""Pack a directory into a .pak for the SDK's pakfs (see sdk/pakfs.h).

    make_pakfs.py <dir> <out.pak>

Every regular file under <dir> is stored under its relative path (max 47
chars). Data 4-byte aligned; >=2 trailing pad bytes (the Pocket never delivers
a file's final 2 bytes — the APF EOF wedge — so nothing real may live there).
"""
import sys
from pathlib import Path

MAGIC, VERSION, ENTRY = 0x464B4150, 1, 56


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    root, out = Path(sys.argv[1]), Path(sys.argv[2])
    files = sorted(p for p in root.rglob("*") if p.is_file())
    if not files:
        sys.exit(f"no files under {root}")
    names = [str(p.relative_to(root)) for p in files]
    for n in names:
        if len(n.encode()) > 47:
            sys.exit(f"name too long (>47): {n}")

    head = bytearray()
    head += MAGIC.to_bytes(4, "little") + VERSION.to_bytes(4, "little")
    head += len(files).to_bytes(4, "little") + b"\0\0\0\0"

    off = 16 + ENTRY * len(files)
    off = (off + 3) & ~3
    blobs, dirents = [], bytearray()
    for p, n in zip(files, names):
        data = p.read_bytes()
        dirents += n.encode().ljust(48, b"\0")
        dirents += off.to_bytes(4, "little") + len(data).to_bytes(4, "little")
        blobs.append((off, data))
        off = (off + len(data) + 3) & ~3

    img = bytearray(off)
    img[0:16] = head
    img[16:16 + len(dirents)] = dirents
    for o, d in blobs:
        img[o:o + len(d)] = d
    img += b"\0\0\0\0"                      # EOF-wedge pad (>=2)
    out.write_bytes(img)
    print(f"{out}: {len(files)} files, {len(img)} bytes")
    for n, (o, d) in zip(names, blobs):
        print(f"  {n:<40} @{o:#08x} {len(d)} B")


if __name__ == "__main__":
    main()
