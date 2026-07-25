#!/usr/bin/env python3
"""Compare two Pocket bitstreams (.rbf or .rbf_r) and say whether they are the SAME FIT.

The rbf is compressed, so two builds that differ anywhere diverge for the rest of the
file -- a plain byte-equality test is useless. What separates "same placement, different
build stamps" from "different placement" is the length of the single longest run of
identical bytes:

    same fit      -> one unbroken run of ~1.1-1.2 MB, size delta of a few bytes
    different fit -> longest run ~21-23 KB,           size delta of thousands

See soc/REPRODUCIBILITY.md.

usage: compare_bitstream.py <reference.rbf_r> <candidate.rbf_r>
SPDX-License-Identifier: BSD-2-Clause
"""
import sys


def longest_runs(a, b):
    n = min(len(a), len(b))
    runs, i = [], 0
    while i < n:
        if a[i] == b[i]:
            j = i
            while j < n and a[j] == b[j]:
                j += 1
            runs.append((i, j - i))
            i = j
        else:
            i += 1
    return runs


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    a = open(sys.argv[1], "rb").read()
    b = open(sys.argv[2], "rb").read()
    print(f"reference : {len(a):>10,} B  {sys.argv[1]}")
    print(f"candidate : {len(b):>10,} B  {sys.argv[2]}   delta {len(b)-len(a):+,}")
    if a == b:
        print("\nBYTE-IDENTICAL")
        return 0
    runs = longest_runs(a, b)
    equal = sum(l for _, l in runs)
    runs.sort(key=lambda r: -r[1])
    n = min(len(a), len(b))
    print(f"equal at same offset : {equal:,}/{n:,} = {equal/n*100:.1f}%")
    print(f"longest identical run: {runs[0][1]:,} B at offset {runs[0][0]:,}")
    verdict = "SAME FIT (differs only in build stamps)" if runs[0][1] > 500_000 \
        else "DIFFERENT FIT"
    print(f"\nverdict: {verdict}")
    return 0 if runs[0][1] > 500_000 else 1


if __name__ == "__main__":
    sys.exit(main())
