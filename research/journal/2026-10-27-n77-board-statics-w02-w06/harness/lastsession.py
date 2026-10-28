#!/usr/bin/env python3
"""lastsession.py <hash-log> <out>: the last session of a --hash-log (the lines after its last frame-counter reset)."""
import sys
lines = open(sys.argv[1]).read().splitlines()
start = 0
prev = -1
for i, l in enumerate(lines):
    f = int(l.split()[0])
    if f < prev:
        start = i
    prev = f
open(sys.argv[2], 'w').write('\n'.join(lines[start:]) + '\n')
print('%s: %d lines from %s' % (sys.argv[1], len(lines) - start, lines[start].split()[0]))
