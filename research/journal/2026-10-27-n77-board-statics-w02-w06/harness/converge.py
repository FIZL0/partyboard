#!/usr/bin/env python3
"""converge.py <pairdiff.txt> [prefix]: for each symbol (default board/ statics) that differs at the first dump after
the board's init (f300), the last dump key at which it still differs and the first after that where it is equal."""
import sys
blocks = []
cur = None
for line in open(sys.argv[1]):
    if line.startswith('--- '):
        cur = (line[4:].strip(), set())
        blocks.append(cur)
    elif cur and line.strip():
        cur[1].add(line.split()[0])
pre = sys.argv[2] if len(sys.argv) > 2 else 'board/'
keys = [k for k, _ in blocks]
i300 = keys.index('f300-ovl-1.bin')
for sym in sorted(s for s in blocks[i300][1] if s.startswith(pre)):
    last = max(i for i, (_, s) in enumerate(blocks) if sym in s)
    eq = keys[last + 1] if last + 1 < len(keys) else 'never (end of run)'
    print('%-40s differs to %-18s equal from %s' % (sym, keys[last], eq))
