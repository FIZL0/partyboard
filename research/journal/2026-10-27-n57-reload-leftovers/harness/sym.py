# sym.py <out> <builddir>: per board-start dump, the used blocks grouped by caller chain (addr2line'd)
import sys, re, subprocess, collections
out, bd = sys.argv[1], sys.argv[2]
dumps = []; cur = None
for line in open(out, errors='replace'):
    if line.startswith('n57:'):
        if cur is None: cur = []; dumps.append(cur)
        cur.append(line.split()[1:])
    elif cur is not None and 'board overlay' in line:
        cur = None
cache = {}
def sym(t):
    m = re.match(r'(.+)\+([0-9a-f]+)$', t)
    if not m or not m.group(1).endswith('.so') or 'libc' in m.group(1): return t
    lib, off = m.group(1), int(m.group(2), 16) - 1
    k = (lib, off)
    if k not in cache:
        r = subprocess.run(['addr2line', '-f', '-s', '-i', '-e', f'{bd}/{lib}', hex(off)], capture_output=True, text=True).stdout.split('\n')
        cache[k] = r[0] + '@' + r[1]
    return cache[k]
for n, d in enumerate(dumps):
    g = collections.defaultdict(lambda: [0, 0])
    for f in d:
        heap, size = f[1], int(f[5], 16)
        chain = ' < '.join(sym(t) for t in f[8:13])
        g[(heap, chain)][0] += 1; g[(heap, chain)][1] += size
    print(f'== dump {n}: {len(d)} blocks')
    for (heap, chain), (c, s) in sorted(g.items()):
        print(f'  heap {heap} {c:4d} blocks {s:8d} B  {chain}')
