#!/usr/bin/env python3
"""N99: a --hash-log from frame START on, renumbered from 0 and cut to COUNT lines, so two runs whose --match started
at different frames compare with tools/netplay/hashdiff.py.   from.py LOG START COUNT > OUT
The generators (frand, rand8, brand, full) are dropped: the title rolls them a run's own number of times, and the
match reseeds them at its first gameplay point (netplay_point_reseed), not at frame 0."""
import sys

log, start, count = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
with open(log) as f:
    for line in f:
        parts = line.split(" ", 1)
        if len(parts) == 2 and parts[0].isdigit() and start <= int(parts[0]) < start + count:
            rest = " ".join(w for w in parts[1].split() if w.split("=")[0] not in ("frand", "rand8", "brand", "full"))
            print(f"{int(parts[0]) - start} {rest}")
