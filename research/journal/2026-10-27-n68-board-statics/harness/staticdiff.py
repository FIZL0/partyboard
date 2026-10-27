#!/usr/bin/env python3
"""staticdiff.py <libdol.so> <a.bin> <b.bin> [objdir]: libdol data/bss symbols whose bytes differ between two dumps.
With objdir (build/.../CMakeFiles/dol.dir/src/game), only symbols defined in its objects, tagged with the object."""
import bisect, glob, os, struct, subprocess, sys

def load(path):
    segs = []
    with open(path, 'rb') as f:
        while True:
            h = f.read(24)
            if len(h) < 24:
                break
            base, vaddr, n = struct.unpack('<QQQ', h)
            segs.append((vaddr, f.read(n)))
    return segs

def syms(so):
    out = subprocess.run(['nm', '-S', '--defined-only', so], capture_output=True, text=True).stdout
    t = []
    for line in out.splitlines():
        p = line.split()
        if len(p) == 4 and p[2] in 'bBdDsSgG':
            t.append((int(p[0], 16), int(p[1], 16), p[3]))
    t.sort()
    return t

so, a, b = sys.argv[1:4]
own = None
if len(sys.argv) > 4:
    own = {}
    for o in glob.glob(sys.argv[4] + '/**/*.o', recursive=True):
        for line in subprocess.run(['nm', '--defined-only', o], capture_output=True, text=True).stdout.splitlines():
            p = line.split()
            if len(p) == 3 and p[1] in 'bBdDsSgG':
                own.setdefault(p[2], os.path.relpath(o, sys.argv[4]).replace('.c.o', '.c'))
A, B = load(a), load(b)
S = syms(so)
starts = [s[0] for s in S]
hits = {}
for (va, da), (vb, db) in zip(A, B):
    assert va == vb and len(da) == len(db)
    for i in range(0, len(da), 8):
        if da[i:i + 8] != db[i:i + 8]:
            for j in range(i, min(i + 8, len(da))):
                if da[j] != db[j]:
                    addr = va + j
                    k = bisect.bisect_right(starts, addr) - 1
                    if k >= 0 and S[k][0] <= addr < S[k][0] + max(S[k][1], 1):
                        name = S[k][2]
                        off = addr - S[k][0]
                    else:
                        name = '?%x' % addr
                        off = 0
                    hits.setdefault(name, []).append((off, da[j], db[j]))
for name, h in sorted(hits.items()):
    if own is not None:
        base = name.split('.')[0] if name not in own else name
        if base not in own:
            continue
        name = '%s:%s' % (own[base], name)
    offs = sorted(set(o for o, _, _ in h))
    print('%-40s %4d bytes, offsets %s' % (name, len(h), ','.join(hex(o) for o in offs[:12]) + (' ...' if len(offs) > 12 else '')))
