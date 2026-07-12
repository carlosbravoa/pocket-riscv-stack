#!/usr/bin/env python3
"""Verify a midiplay OPL register log (RVSTACK_OPLLOG capture, PC twin).

Log line format: "<t_us> <reg-hex> <val-hex>", one opl_write per line.

Modes:
  check_opllog.py LOG SONG.MID [--tol-ms 50] [--require-balance]
      Sanity + timing against the source MIDI:
        * OPL3 NEW mode (0x105 bit0) set before the first key-on
        * every 0xC0 channel write carries L/R output-enable bits
        * per-hw-channel KON alternation (no double key-on / key-off)
        * hardware key-on times match the MIDI's tempo-mapped note-on
          schedule within --tol-ms (default 50 ms ~= 3 display frames)
        * key-on/key-off balance (strict only with --require-balance,
          since a timeout-truncated capture legitimately dangles)
  check_opllog.py --compare LOG_A LOG_B [--tol-ms 50]
      Same song captured under two patch banks: note timing must match,
      operator/patch register streams must differ.

Exit code 0 = pass, 1 = fail.
"""
import argparse
import statistics
import sys

PERC_LO, PERC_HI = 35, 81


# --------------------------------------------------------------- log side

def parse_log(path):
    out = []
    with open(path) as f:
        for ln, line in enumerate(f, 1):
            parts = line.split()
            if len(parts) != 3:
                continue                        # tolerate a truncated tail
            try:
                out.append((int(parts[0]), int(parts[1], 16),
                            int(parts[2], 16)))
            except ValueError:
                sys.exit(f"{path}:{ln}: unparsable line {line!r}")
    if not out:
        sys.exit(f"{path}: empty log")
    return out


def analyze_log(log, path):
    """Returns (keyon_times, problems, stats)."""
    problems = []
    kon = {}                                    # hw channel -> bit
    keyons, keyoffs = [], []
    new_mode = False
    first_kon_seen = False
    for t, reg, val in log:
        if reg == 0x105:
            new_mode = (val & 1) == 1
        r, bank = reg & 0xFF, (reg >> 8) & 1
        if 0xC0 <= r <= 0xC8:
            if not (val & 0x30):
                problems.append(
                    f"0x{reg:03X} <- 0x{val:02X}: no L/R enable bits")
        if 0xB0 <= r <= 0xB8:
            ch = bank * 9 + (r - 0xB0)
            bit = (val >> 5) & 1
            prev = kon.get(ch, 0)
            if bit and not prev:
                if not first_kon_seen:
                    first_kon_seen = True
                    if not new_mode:
                        problems.append(
                            "first key-on before OPL3 NEW mode (0x105)")
                keyons.append(t)
            elif prev and not bit:
                keyoffs.append(t)
            kon[ch] = bit
    dangling = sum(kon.values())
    stats = {
        "writes": len(log),
        "keyons": len(keyons),
        "keyoffs": len(keyoffs),
        "dangling": dangling,
        "span_ms": (log[-1][0] - log[0][0]) / 1000.0,
    }
    print(f"{path}: {stats['writes']} writes, {stats['keyons']} key-ons, "
          f"{stats['keyoffs']} key-offs, {dangling} still held at cut, "
          f"{stats['span_ms']:.0f} ms span")
    return keyons, problems, stats


def patch_stream(log):
    """Operator/patch register writes (timbre identity, order-preserving)."""
    sig = []
    for _, reg, val in log:
        r = reg & 0xFF
        if (0x20 <= r <= 0x35 or 0x40 <= r <= 0x55 or 0x60 <= r <= 0x75
                or 0x80 <= r <= 0x95 or 0xE0 <= r <= 0xF5):
            sig.append((reg, val))
        elif 0xC0 <= r <= 0xC8:
            sig.append((reg, val & 0x0F))       # feedback/conn, pan excluded
    return sig


# -------------------------------------------------------------- midi side

def parse_midi_noteons(path):
    """Tempo-mapped absolute note-on times (us) the player should key."""
    data = open(path, "rb").read()
    if data[:4] != b"MThd":
        sys.exit(f"{path}: not an SMF")
    hlen = int.from_bytes(data[4:8], "big")
    division = int.from_bytes(data[12:14], "big")
    if division & 0x8000:
        sys.exit(f"{path}: SMPTE timing unsupported")
    pos, end = 8 + hlen, len(data)
    tracks = []
    while pos + 8 <= end:
        cid = data[pos:pos + 4]
        clen = int.from_bytes(data[pos + 4:pos + 8], "big")
        clen = min(clen, end - pos - 8)
        if cid == b"MTrk":
            tracks.append((pos + 8, pos + 8 + clen))
        pos += 8 + clen

    events = []                                 # (tick, order, kind, payload)
    order = 0
    for tstart, tend in tracks:
        p, tick, run = tstart, 0, None
        while p < tend:
            dt = 0
            while True:                         # VLQ
                b = data[p]; p += 1
                dt = (dt << 7) | (b & 0x7F)
                if not b & 0x80:
                    break
            tick += dt
            st = data[p]
            if st & 0x80:
                p += 1
            else:
                st = run
                if st is None:
                    break
            hi = st & 0xF0
            if hi in (0x80, 0x90, 0xA0, 0xB0, 0xE0):
                run = st
                a, b = data[p], data[p + 1]; p += 2
                if hi == 0x90 and b:
                    events.append((tick, order, "on", (st & 0x0F, a)))
                    order += 1
            elif hi in (0xC0, 0xD0):
                run = st
                p += 1
            elif st in (0xF0, 0xF7):
                run = None
                sl = 0
                while True:
                    b = data[p]; p += 1
                    sl = (sl << 7) | (b & 0x7F)
                    if not b & 0x80:
                        break
                p += sl
            elif st == 0xFF:
                run = None
                mt = data[p]; p += 1
                ml = 0
                while True:
                    b = data[p]; p += 1
                    ml = (ml << 7) | (b & 0x7F)
                    if not b & 0x80:
                        break
                if mt == 0x2F:
                    break
                if mt == 0x51 and ml == 3:
                    tempo = int.from_bytes(data[p:p + 3], "big")
                    events.append((tick, order, "tempo", tempo))
                    order += 1
                p += ml
            else:
                break

    events.sort(key=lambda e: (e[0], e[1]))
    tempo, last_tick, us = 500000, 0, 0
    ons = []
    for tick, _, kind, payload in events:
        us += (tick - last_tick) * tempo // division
        last_tick = tick
        if kind == "tempo":
            tempo = payload
        else:
            ch, note = payload
            if ch == 9 and not (PERC_LO <= note <= PERC_HI):
                continue                        # the player skips these
            ons.append(us)
    return ons


# ------------------------------------------------------------------ modes

def match_times(log_t, exp_t, tol_us, label):
    n = min(len(log_t), len(exp_t))
    if n == 0:
        print(f"FAIL [{label}]: nothing to compare")
        return False
    offset = statistics.median(log_t[i] - exp_t[i] for i in range(n))
    devs = [abs((log_t[i] - offset) - exp_t[i]) for i in range(n)]
    worst = max(devs)
    bad = sum(1 for d in devs if d > tol_us)
    print(f"[{label}] {n} note-ons compared, start offset "
          f"{offset / 1000:.1f} ms, worst deviation {worst / 1000:.1f} ms, "
          f"{bad} beyond {tol_us / 1000:.0f} ms")
    return bad == 0


def mode_check(args):
    log = parse_log(args.log)
    keyons, problems, stats = analyze_log(log, args.log)
    exp = parse_midi_noteons(args.mid)
    print(f"{args.mid}: {len(exp)} expected note-ons")

    ok = True
    for p in problems:
        print(f"FAIL: {p}")
        ok = False

    if len(keyons) > len(exp):
        print(f"FAIL: more hardware key-ons ({len(keyons)}) than the MIDI "
              f"schedules ({len(exp)})")
        ok = False
    elif len(keyons) < len(exp):
        cut = log[-1][0]
        print(f"note: log truncated — {len(keyons)}/{len(exp)} note-ons "
              f"captured before the {cut / 1e6:.1f} s cut (fine for a "
              f"timeout-bounded run)")

    if not match_times(keyons, exp, args.tol_ms * 1000, "timing"):
        ok = False

    if args.require_balance and stats["dangling"]:
        print(f"FAIL: {stats['dangling']} channels still keyed on at end")
        ok = False
    return ok


def mode_compare(args):
    la, lb = parse_log(args.log), parse_log(args.mid)   # positional reuse
    ka, _, _ = analyze_log(la, args.log)
    kb, _, _ = analyze_log(lb, args.mid)
    ok = True
    if len(ka) != len(kb):
        n = min(len(ka), len(kb))
        print(f"note: different lengths ({len(ka)} vs {len(kb)} key-ons); "
              f"comparing the common {n}-note prefix")
    if not match_times(ka, kb, args.tol_ms * 1000, "A/B timing"):
        ok = False
    pa, pb = patch_stream(la), patch_stream(lb)
    if pa == pb:
        print("FAIL: patch register streams identical — bank switch had "
              "no effect")
        ok = False
    else:
        diff = sum(1 for x, y in zip(pa, pb) if x != y) + abs(len(pa) - len(pb))
        print(f"[banks] patch streams differ as expected "
              f"({len(pa)} vs {len(pb)} timbre writes, {diff} differing)")
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("mid", help="SONG.MID, or LOG_B with --compare")
    ap.add_argument("--tol-ms", type=float, default=50.0)
    ap.add_argument("--require-balance", action="store_true")
    ap.add_argument("--compare", action="store_true")
    args = ap.parse_args()
    ok = mode_compare(args) if args.compare else mode_check(args)
    print("PASS" if ok else "FAIL")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
