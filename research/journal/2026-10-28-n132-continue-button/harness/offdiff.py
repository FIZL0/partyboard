#!/usr/bin/env python3
"""offdiff.py a.log b.log: lines up two --hash-log runs at their first board frame (ovl 89..95) and prints the first
frame whose state hash (column 2) differs, with the sections that differ."""
import sys

def load(path):
    rows = []
    for line in open(path):
        p = line.split()
        if len(p) > 3 and p[0].isdigit():
            rows.append((int(p[0]), p[1], dict(kv.split("=", 1) for kv in p[2:] if "=" in kv)))
    return rows

def first_board(rows):
    return next(i for i, r in enumerate(rows) if 89 <= int(r[2]["ovl"]) <= 95)

a, b = load(sys.argv[1]), load(sys.argv[2])
ia, ib = first_board(a), first_board(b)
print(f"first board frame: {a[ia][0]} / {b[ib][0]}")
n = 0
while ia + n < len(a) and ib + n < len(b):
    ra, rb = a[ia + n], b[ib + n]
    if ra[1] != rb[1]:
        diff = [k for k in ra[2] if k not in ("frand", "rand8", "full", "heap") and ra[2][k] != rb[2].get(k)]
        print(f"first differing hash after {n} board frames: frames {ra[0]} / {rb[0]}, sections {diff}")
        sys.exit(0)
    n += 1
print(f"equal for all {n} lined-up frames")
