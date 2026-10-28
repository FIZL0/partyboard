#!/usr/bin/env python3
"""shift.py <script> <frames>: the --script with every pad line <frames> earlier (lines before frame 0 dropped, but each
port's last state before 0 kept at frame 0) and its end moved alike, so a Continue run gets the pads a menu-path run
gets after the same board frame."""
import sys

src, by = sys.argv[1], int(sys.argv[2])
held = {}
out = []
for line in open(src):
    p = line.split()
    if not p or p[0].startswith("#"):
        continue
    if p[0] == "end":
        out.append(f"end {int(p[1]) - by}")
    elif p[0].isdigit():
        f = int(p[0]) - by
        if f < 0:
            held[p[1]] = " ".join(p[1:])
            continue
        for rest in held.values():
            out.append(f"0 {rest}")
        held = {}
        out.append(" ".join([str(f)] + p[1:]))
    else:
        out.append(line.strip())
print("\n".join(out))
