#!/usr/bin/env python3
"""Bit-exact reference for voice_mixer.v (the law in README.md), plus bench
vector generation: tb_samples.txt (byte RAM), tb_cfg.txt (voice configs),
tb_gold.txt (expected L,R per frame).

Pure integer math; every >> is a floor shift exactly like Verilog's >>> on the
sliced products. stdlib only.
"""
import random

NV_USED   = 12          # voices exercised (core has 32)
N_FRAMES  = 2000
MASTER    = 0xE0        # exercise master scaling (not unity)
MEM_BYTES = 65536

random.seed(20260726)   # deterministic vectors

def s16(x):  # wrap to signed 16
    x &= 0xFFFF
    return x - 0x10000 if x & 0x8000 else x

def floor_shift(x, n):  # arithmetic >> (python's >> already floors)
    return x >> n

# ---------------------------------------------------------------- samples
mem = bytearray(MEM_BYTES)

def put_s8(addr, vals):
    for i, v in enumerate(vals): mem[addr+i] = v & 0xFF

def put_u8(addr, vals):
    for i, v in enumerate(vals): mem[addr+i] = v & 0xFF

def put_s16(addr, vals):
    for i, v in enumerate(vals):
        mem[addr+2*i]   = v & 0xFF
        mem[addr+2*i+1] = (v >> 8) & 0xFF

# a few deterministic waveforms
def tri(n, amp):   # triangle
    out=[]
    for i in range(n):
        ph = (i % n) / n
        v  = 4*ph-1 if ph < 0.5 else 3-4*ph
        out.append(int(v*amp))
    return out

def noise(n, amp):
    return [random.randint(-amp, amp) for _ in range(n)]

def ramp(n, amp):
    return [int(-amp + (2*amp*i)//max(1,n-1)) for i in range(n)]

# layout: (addr, fmt, data)  fmt: 0 s8, 1 u8, 2 s16
S8A  = 0x0000; put_s8 (S8A,  tri(256, 120))
S8B  = 0x0100; put_s8 (S8B,  noise(500, 127))
U8A  = 0x0400; put_u8 (U8A,  [v+128 for v in tri(300, 120)])
S16A = 0x0800; put_s16(S16A, tri(512, 30000))
S16B = 0x1000; put_s16(S16B, noise(700, 32767))
S16C = 0x2000; put_s16(S16C, ramp(64, 32000))

# ---------------------------------------------------------------- voices
# (base, len, fmt, loop, ls, le, step_8_16, vol, pan)
V = [
    # loops across formats, pitches up and down
    (S8A,  256, 0, 1,   0, 256, 0x00_8000, 110, 128),  # s8 fwd loop, 0.5x
    (S8A,  256, 0, 1,  64, 192, 0x01_8000, 130,   0),  # s8 inner loop, 1.5x, hard L
    (U8A,  300, 1, 1,   0, 300, 0x00_C000, 120, 255),  # u8 loop, 0.75x, hard R
    (S16A, 512, 2, 1,   0, 512, 0x02_0000, 120, 128),  # s16 loop, 2x
    (S16A, 512, 2, 1, 100, 400, 0x00_5555, 100,  40),  # s16 inner loop, odd step
    (S16B, 700, 2, 1,   0, 700, 0x03_1234, 110, 210),  # s16 noise, >3x pitch-up
    # one-shots that END inside the run (exercise deactivation + s1 clamp)
    (S16C,  64, 2, 0,   0,   0, 0x00_4000, 150, 128),  # ends ~frame 256
    (S8B,  500, 0, 0,   0,   0, 0x01_0000, 130,  60),  # ends at frame 500
    (S16B, 700, 2, 0,   0,   0, 0x02_0000, 100, 128),  # ends at frame 350
    # saturation stress: loud, same pan side (tuned for ~5-10% clipped frames)
    (S16A, 512, 2, 1,   0, 512, 0x01_0000, 120,  20),
    (S16A, 512, 2, 1,   0, 512, 0x01_0001, 120,  20),
    (S16B, 700, 2, 1,   0, 700, 0x00_FFFF, 90,   30),
]
assert len(V) == NV_USED

def fetch(base, fmt, idx):
    if fmt == 2:
        a = base + 2*idx
        return s16(mem[a] | (mem[a+1] << 8))
    b = mem[base + idx]
    if fmt == 0:
        return s16((b if b < 128 else b-256) << 8) if False else ((b - 256 if b >= 128 else b) << 8)
    return (b - 128) << 8

# ---------------------------------------------------------------- reference mix
pos    = [0]*NV_USED
active = [True]*NV_USED
gold   = []

for _ in range(N_FRAMES):
    acc_l = 0; acc_r = 0
    for v in range(NV_USED):
        if not active[v]:
            continue
        base, ln, fmt, loop, ls, le, step, vol, pan = V[v]
        idx0 = pos[v] >> 16
        frac = pos[v] & 0xFFFF
        if loop == 1:
            idx1 = idx0 + 1
            if idx1 >= le: idx1 = ls
        else:
            idx1 = min(idx0 + 1, ln - 1)
        s0 = fetch(base, fmt, idx0)
        s1v = fetch(base, fmt, idx1)
        s   = s0 + floor_shift((s1v - s0) * frac, 16)
        sv  = floor_shift(s * vol, 8)
        l   = floor_shift(sv * (255 - pan), 8)
        r   = floor_shift(sv * pan, 8)
        acc_l += l; acc_r += r
        np_ = pos[v] + step
        if loop == 1:
            if (np_ >> 16) >= le:
                np_ -= (le - ls) << 16
        else:
            if (np_ >> 16) >= ln:
                active[v] = False
        pos[v] = np_
    ml = floor_shift(acc_l * MASTER, 8)
    mr = floor_shift(acc_r * MASTER, 8)
    ml = max(-32768, min(32767, ml))
    mr = max(-32768, min(32767, mr))
    gold.append((ml, mr))

# ---------------------------------------------------------------- emit
with open("tb_samples.txt", "w") as f:
    for b in mem:
        f.write(f"{b:02x}\n")

with open("tb_cfg.txt", "w") as f:
    # one line per voice: base len fmt loop ls le step vol pan (hex)
    for (base, ln, fmt, loop, ls, le, step, vol, pan) in V:
        f.write(f"{base:07x} {ln:06x} {fmt:x} {loop:x} {ls:06x} {le:06x} "
                f"{step:06x} {vol:02x} {pan:02x}\n")

with open("tb_gold.txt", "w") as f:
    for (l, r) in gold:
        f.write(f"{l & 0xFFFF:04x}\n{r & 0xFFFF:04x}\n")

n_active_end = sum(active)
print(f"gen_ref: {N_FRAMES} frames, {NV_USED} voices ({n_active_end} still active "
      f"at end), master=0x{MASTER:02x}")
print(f"gold[0..3] = {gold[:4]}")
print(f"gold[-2:]  = {gold[-2:]}")
sat = sum(1 for l, r in gold if l in (-32768, 32767) or r in (-32768, 32767))
print(f"saturated frames: {sat} (want >0 to prove clamping)")
