#!/usr/bin/env python3
"""Generate Pocket artwork for the RISC-V stack core: Cores/.../icon.bin (36x36)
and Platforms/_images/riscv_stack.bin (521x165).

Format (deduced from shipped cores, same as spc-pocket-player's make_artwork.py):
2 bytes/pixel, brightness in byte 0, byte 1 zero; the file stores the image
rotated 90 degrees counter-clockwise (file pixel (row r, col c) = original
pixel (x = W-1-r, y = c)).
"""
import re
from pathlib import Path

FONT_H = Path("/home/carlos/devel/fpga/spc-pocket-player/tools/font8x8_basic.h")
glyphs = re.findall(
    r"\{\s*((?:0x[0-9A-Fa-f]{2}\s*,\s*){7}0x[0-9A-Fa-f]{2})\s*\}", FONT_H.read_text())
FONT = {chr(i): [int(x, 16) for x in re.split(r"\s*,\s*", glyphs[i])] for i in range(128)}


def draw_text(img, W, H, text, x0, y0, scale, val=255):
    for ci, ch in enumerate(text):
        g = FONT.get(ch, FONT[" "])
        for ry in range(8):
            for rx in range(8):
                if g[ry] >> rx & 1:
                    for sy in range(scale):
                        for sx in range(scale):
                            x = x0 + (ci * 8 + rx) * scale + sx
                            y = y0 + ry * scale + sy
                            if 0 <= x < W and 0 <= y < H:
                                img[y][x] = val


def text_w(text, scale):
    return len(text) * 8 * scale


def draw_chip(img, W, H, x0, y0, s, val=255):
    """Simple chip motif: filled die with notch, 8 pins per side. 24x24 design box."""
    for y in range(24 * s):
        for x in range(24 * s):
            u, v = x / s, y / s
            on = False
            if 5 <= u <= 19 and 5 <= v <= 19:               # body
                on = not (16.5 <= u <= 19 and 5 <= v <= 7.5)  # corner notch
            for i in range(4):                                # pins
                p = 6.5 + i * 3.4
                if p <= u <= p + 1.6 and (2 <= v <= 5 or 19 <= v <= 22):
                    on = True
                if p <= v <= p + 1.6 and (2 <= u <= 5 or 19 <= u <= 22):
                    on = True
            if on:
                px, py = x0 + x, y0 + y
                if 0 <= px < W and 0 <= py < H:
                    img[py][px] = val


def emit(img, W, H, path):
    out = bytearray()
    for r in range(W):
        for c in range(H):
            out.append(img[c][W - 1 - r])
            out.append(0)
    Path(path).write_bytes(out)
    print(f"wrote {path} ({len(out)} bytes)")


OUT = Path("/home/carlos/devel/fpga/riscv-stack/soc/spc_clone/out")

# Platform image 521x165: chip motif + "RISC-V STACK".
W, H = 521, 165
img = [[0] * W for _ in range(H)]
title, ts = "RISC-V STACK", 4
sub, ss = "SOFT CPU + FRAMEBUFFER + HAL", 1
chip_s = 3  # 72x72 chip
total_w = 24 * chip_s + 16 + text_w(title, ts)
x0 = (W - total_w) // 2
draw_chip(img, W, H, x0, (H - 24 * chip_s) // 2 - 8, chip_s)
tx = x0 + 24 * chip_s + 16
draw_text(img, W, H, title, tx, (H - 8 * ts) // 2 - 14, ts)
draw_text(img, W, H, sub, tx + 2, (H - 8 * ts) // 2 - 14 + 8 * ts + 10, ss, val=140)
emit(img, W, H, OUT / "Platforms/_images/riscv_stack.bin")

# Icon 36x36: small chip.
W, H = 36, 36
img = [[0] * W for _ in range(H)]
draw_chip(img, W, H, (W - 24) // 2 - 1, (H - 24) // 2 - 1, 1)
draw_text(img, W, H, "RV", 10, 14, 1, val=0)   # punched dark into the white die
emit(img, W, H, OUT / "Cores/bravo.RiscvStack/icon.bin")
