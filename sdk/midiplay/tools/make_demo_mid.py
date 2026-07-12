#!/usr/bin/env python3
"""Generate demo_groove.mid — a self-authored format-0 test song (CC0).

    make_demo_mid.py assets/demo_groove.mid

Written to exercise player paths the classical test set doesn't touch:
format 0, percussion on channel 10, program changes, running status from
the delta-time packing, a mid-song tempo change, and pitch bends.
8 bars: drum groove + fingered bass + square lead.
"""
import sys


def vlq(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.append(0x80 | (n & 0x7F))
        n >>= 7
    return bytes(reversed(out))


DIV = 480                              # ticks per quarter
ev = []                                # (abs_tick, bytes)


def at(tick, *data):
    ev.append((tick, bytes(data)))


def note(tick, ch, key, vel, dur):
    at(tick, 0x90 | ch, key, vel)
    at(tick + dur, 0x80 | ch, key, 0)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)

    at(0, 0xFF, 0x51, 3, 0x07, 0xA1, 0x20)     # 120 bpm
    at(0, 0xC0, 33)                            # ch1: fingered bass
    at(0, 0xC1, 80)                            # ch2: square lead
    at(0, 0xB0, 7, 110)
    at(0, 0xB1, 7, 96)
    at(0, 0xB9, 7, 120)

    q = DIV                                    # quarter
    e = DIV // 2                               # eighth
    bass_line = [36, 36, 43, 41, 36, 36, 39, 41]
    lead_line = [60, 63, 65, 67, 63, 60, 58, 60,
                 67, 70, 72, 70, 67, 65, 63, 60]

    for bar in range(8):
        t0 = bar * 4 * q
        for beat in range(4):                  # drums, ch10 (idx 9)
            tb = t0 + beat * q
            if beat in (0, 2):
                note(tb, 9, 36, 118, e)        # kick
            else:
                note(tb, 9, 38, 105, e)        # snare
            note(tb, 9, 42, 70, e)             # closed hat x2
            note(tb + e, 9, 42, 58, e)
        for i in range(8):                     # bass, eighths
            note(t0 + i * e, 0, bass_line[i], 100, e - 30)
        if bar >= 2:                           # lead enters at bar 3
            for i in range(8):
                key = lead_line[(bar % 2) * 8 + i]
                note(t0 + i * e, 1, key, 88, e - 20)
        if bar == 5:                           # bend up and back on the lead
            at(t0, 0xE1, 0, 0x50)
            at(t0 + q, 0xE1, 0, 0x40)

    at(4 * 4 * q, 0xFF, 0x51, 3, 0x06, 0x1A, 0x80)   # bar 5: 150 bpm

    ev.sort(key=lambda p: p[0])
    trk = bytearray()
    last = 0
    for tick, data in ev:
        trk += vlq(tick - last) + data
        last = tick
    trk += vlq(0) + bytes([0xFF, 0x2F, 0x00])

    out = bytearray(b"MThd") + (6).to_bytes(4, "big")
    out += (0).to_bytes(2, "big") + (1).to_bytes(2, "big")
    out += DIV.to_bytes(2, "big")
    out += b"MTrk" + len(trk).to_bytes(4, "big") + trk
    open(sys.argv[1], "wb").write(out)
    print(f"{sys.argv[1]}: {len(out)} bytes, {len(ev)} events")


if __name__ == "__main__":
    main()
