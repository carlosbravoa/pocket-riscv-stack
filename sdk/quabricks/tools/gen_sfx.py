#!/usr/bin/env python3
"""Synthesize modern, clean Tetris SFX using only the Python stdlib.
16-bit mono WAV @ 44100. Output dir passed as argv[1]."""
import wave, struct, math, sys, os

SR = 44100
OUT = sys.argv[1] if len(sys.argv) > 1 else "."

def env_adsr(n, i, a, d, s, r, total):
    """Simple linear ADSR envelope value at sample i of n."""
    t = i / SR
    A, D, R = a, d, r
    sustain_end = total - R
    if t < A:
        return t / A if A > 0 else 1.0
    if t < A + D:
        return 1.0 - (1.0 - s) * ((t - A) / D) if D > 0 else s
    if t < sustain_end:
        return s
    if t < total:
        return s * max(0.0, 1.0 - (t - sustain_end) / R) if R > 0 else 0.0
    return 0.0

def sine(f, t):   return math.sin(2 * math.pi * f * t)
def tri(f, t):
    x = (f * t) % 1.0
    return 4 * abs(x - 0.5) - 1
def square(f, t): return 1.0 if (f * t) % 1.0 < 0.5 else -1.0
def saw(f, t):    return 2 * ((f * t) % 1.0) - 1

# deterministic pseudo-noise (no random import needed / reproducible)
_seed = [0x1234567]
def noise():
    _seed[0] = (1103515245 * _seed[0] + 12345) & 0x7fffffff
    return (_seed[0] / 0x3fffffff) - 1.0

def render(dur, fn):
    """fn(t, i, n) -> sample in [-1,1]."""
    n = int(dur * SR)
    return [fn(i / SR, i, n) for i in range(n)]

def mix(*layers):
    n = max(len(l) for l in layers)
    out = [0.0] * n
    for l in layers:
        for i, v in enumerate(l):
            out[i] += v
    return out

def soft_clip(x):
    return math.tanh(x * 1.2)

def save(name, samples, gain=0.85):
    # normalize
    peak = max(1e-9, max(abs(s) for s in samples))
    scale = gain / peak
    path = os.path.join(OUT, name + ".wav")
    with wave.open(path, "w") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        frames = bytearray()
        for s in samples:
            v = int(max(-1.0, min(1.0, s * scale)) * 32767)
            frames += struct.pack("<h", v)
        w.writeframes(frames)
    print(f"  {name}.wav  {len(samples)/SR*1000:.0f}ms")

def blip(freq, dur, a=0.002, d=0.02, s=0.3, r=0.03, wave_fn=sine, vib=0):
    total = dur
    def f(t, i, n):
        ff = freq * (1 + vib * math.sin(2 * math.pi * 30 * t))
        e = env_adsr(n, i, a, d, s, r, total)
        return wave_fn(ff, t) * e
    return render(dur, f)

def glide(f0, f1, dur, wave_fn=sine, a=0.003, r=0.05):
    def f(t, i, n):
        frac = t / dur
        ff = f0 * (f1 / f0) ** frac
        e = env_adsr(n, i, a, 0.0, 1.0, r, dur)
        return wave_fn(ff, t) * e
    return render(dur, f)

def chime(freqs, note=0.09, gap=0.05, wave_fn=sine):
    """Arpeggio of pure-ish tones with soft bell decay."""
    total = gap * (len(freqs) - 1) + note + 0.05
    n = int(total * SR)
    out = [0.0] * n
    for k, fr in enumerate(freqs):
        start = int(k * gap * SR)
        ln = int(note * SR)
        for i in range(ln):
            idx = start + i
            if idx >= n: break
            t = i / SR
            e = math.exp(-t * 14)  # bell decay
            # add a soft 2nd harmonic for richness
            out[idx] += (wave_fn(fr, t) * 0.8 + sine(fr * 2, t) * 0.2) * e
    return out

# ---- Individual effects -------------------------------------------------

def sfx_move():
    # crisp short tick
    return blip(680, 0.045, a=0.001, d=0.015, s=0.15, r=0.02, wave_fn=tri)

def sfx_rotate():
    # two quick rising ticks
    a = blip(520, 0.05, a=0.001, d=0.02, s=0.2, r=0.02, wave_fn=tri)
    b = glide(600, 900, 0.06, wave_fn=tri)
    out = mix(a, b + [0]*(len(a)-len(b)) if len(b)<len(a) else b)
    return [soft_clip(x*0.7) for x in out]

def sfx_softdrop():
    return blip(300, 0.035, a=0.001, d=0.012, s=0.1, r=0.015, wave_fn=sine)

def sfx_harddrop():
    # punchy thunk: low body + noise transient
    body = glide(220, 90, 0.16, wave_fn=tri, a=0.001, r=0.09)
    sub = glide(120, 55, 0.16, wave_fn=sine, a=0.001, r=0.09)
    n = int(0.05 * SR)
    click = []
    for i in range(n):
        t = i / SR
        e = math.exp(-t * 60)
        click.append(noise() * e)
    click += [0.0] * (len(body) - len(click))
    out = mix([b*0.7 for b in body], [b*0.9 for b in sub], [c*0.5 for c in click])
    return [soft_clip(x) for x in out]

def sfx_lock():
    # soft mechanical click
    n = int(0.06 * SR)
    click = []
    for i in range(n):
        t = i / SR
        e = math.exp(-t * 45)
        click.append((noise()*0.4 + tri(400, t)*0.6) * e)
    tone = blip(330, 0.07, a=0.001, d=0.03, s=0.15, r=0.03, wave_fn=sine)
    click += [0.0]*(len(tone)-len(click)) if len(click)<len(tone) else []
    tone += [0.0]*(len(click)-len(tone)) if len(tone)<len(click) else []
    return [soft_clip(x) for x in mix(click, [t*0.6 for t in tone])]

def sfx_lineclear():
    # pleasant ascending triad sweep
    return chime([523.25, 659.25, 783.99], note=0.10, gap=0.055, wave_fn=sine)

def sfx_tetris():
    # celebratory arpeggio (C E G C octave) with sparkle
    base = chime([523.25, 659.25, 783.99, 1046.50], note=0.12, gap=0.06, wave_fn=sine)
    spark = chime([1046.5, 1318.5, 1568.0], note=0.08, gap=0.05, wave_fn=tri)
    spark = [s*0.35 for s in spark] + [0.0]*(max(0,len(base)-len(spark)))
    base += [0.0]*(max(0,len(spark)-len(base)))
    return mix(base, spark[:len(base)])

def sfx_levelup():
    # rising sparkle arpeggio
    return chime([659.25, 783.99, 987.77, 1318.51], note=0.10, gap=0.05, wave_fn=tri)

def sfx_gameover():
    # descending sad tones
    return chime([440.0, 349.23, 293.66, 220.0], note=0.22, gap=0.18, wave_fn=sine)

def sfx_menu_move():
    return blip(600, 0.05, a=0.001, d=0.02, s=0.2, r=0.02, wave_fn=tri)

def sfx_menu_select():
    a = blip(660, 0.06, a=0.001, d=0.02, s=0.25, r=0.03, wave_fn=sine)
    b = glide(660, 990, 0.12, wave_fn=sine, a=0.001, r=0.06)
    a += [0.0]*(max(0,len(b)-len(a)))
    b += [0.0]*(max(0,len(a)-len(b)))
    return [soft_clip(x*0.8) for x in mix(a, b)]

def sfx_hold():
    return blip(500, 0.06, a=0.001, d=0.02, s=0.2, r=0.03, wave_fn=tri, vib=0.01)

def sfx_pause():
    return glide(500, 350, 0.10, wave_fn=sine, a=0.002, r=0.05)

EFFECTS = {
    "move": sfx_move, "rotate": sfx_rotate, "softdrop": sfx_softdrop,
    "harddrop": sfx_harddrop, "lock": sfx_lock, "lineclear": sfx_lineclear,
    "tetris": sfx_tetris, "levelup": sfx_levelup, "gameover": sfx_gameover,
    "menu_move": sfx_menu_move, "menu_select": sfx_menu_select,
    "hold": sfx_hold, "pause": sfx_pause,
}

if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    print("Generating SFX ->", OUT)
    for name, fn in EFFECTS.items():
        save(name, fn())
    print("Done.")
