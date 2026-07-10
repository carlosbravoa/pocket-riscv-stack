#!/usr/bin/env python3
"""Generate the demo pak: a raw 320x240 rgb332 image (76800 bytes) + 2 pad bytes
(the APF dataslot-read EOF wedge means the last 2 bytes of a file are never
pulled — every pak ships padded).

Image: sky-to-horizon gradient + a chip motif + "PAK LOADED" text, so a
successful data-slot pull is unmistakable on screen.
"""
import re
from pathlib import Path

W, H = 320, 240

FONT_H = Path("/home/carlos/devel/fpga/spc-pocket-player/tools/font8x8_basic.h")
glyphs = re.findall(
    r"\{\s*((?:0x[0-9A-Fa-f]{2}\s*,\s*){7}0x[0-9A-Fa-f]{2})\s*\}", FONT_H.read_text())
FONT = {chr(i): [int(x, 16) for x in re.split(r"\s*,\s*", glyphs[i])] for i in range(128)}


def rgb332(r, g, b):  # 0-255 each
    return ((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6)


img = bytearray(W * H)

# Vertical gradient: deep blue sky -> orange horizon -> dark ground.
for y in range(H):
    if y < 160:
        t = y / 160
        c = rgb332(int(30 + 200 * t), int(20 + 90 * t), int(120 - 60 * t))
    else:
        t = (y - 160) / 80
        c = rgb332(int(60 - 50 * t), int(160 - 120 * t), int(40 - 30 * t))
    for x in range(W):
        img[y * W + x] = c

# Sun disc.
cx, cy, rr = 250, 150, 28
for y in range(cy - rr, cy + rr + 1):
    for x in range(cx - rr, cx + rr + 1):
        if 0 <= x < W and 0 <= y < H and (x - cx) ** 2 + (y - cy) ** 2 <= rr * rr:
            img[y * W + x] = rgb332(255, 220, 80)


def draw_text(text, x0, y0, scale, col):
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
                                img[y * W + x] = col


t = "PAK LOADED"
draw_text(t, (W - len(t) * 8 * 3) // 2 + 2, 42, 3, rgb332(0, 0, 0))       # shadow
draw_text(t, (W - len(t) * 8 * 3) // 2,     40, 3, rgb332(255, 255, 255))
s = "VIA APF DATA SLOT"
draw_text(s, (W - len(s) * 8 * 1) // 2, 74, 1, rgb332(255, 255, 160))

out = Path("/home/carlos/devel/fpga/riscv-stack/soc/spc_clone/out/Assets/riscv_stack/common/demo.img")
out.write_bytes(bytes(img) + b"\x00\x00")   # +2 pad bytes (EOF wedge)
print(f"wrote {out} ({out.stat().st_size} bytes)")
