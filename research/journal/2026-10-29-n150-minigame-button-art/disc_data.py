# N150: pull every data/*.bin out of a GC .ciso: disc_data.py <disc.ciso> <out dir>
import os, struct, sys
path, out = sys.argv[1], sys.argv[2]
f = open(path, 'rb')
hdr = f.read(0x8000)
assert hdr[:4] == b'CISO'
bs = struct.unpack('<I', hdr[4:8])[0]
idx = {}; n = 0
for i, b in enumerate(hdr[8:]):
    if b: idx[i] = n; n += 1
def read(off, size):
    r = bytearray()
    while size:
        blk, o = divmod(off, bs); k = min(size, bs - o)
        if blk in idx:
            f.seek(0x8000 + idx[blk] * bs + o); r += f.read(k)
        else: r += bytes(k)
        off += k; size -= k
    return bytes(r)
be = lambda b, o: struct.unpack('>I', b[o:o+4])[0]
h = read(0x420, 12); fst, fsz = be(h, 4), be(h, 8)
F = read(fst, fsz); cnt = be(F, 8); strs = cnt * 12
def name(i): o = strs + (be(F, i*12) & 0xFFFFFF); return F[o:F.index(b'\0', o)].decode()
os.makedirs(out, exist_ok=True)
# walk with directory ends to get paths
stack = [(cnt, '')]
for i in range(1, cnt):
    while i >= stack[-1][0]: stack.pop()
    p = stack[-1][1] + name(i)
    if F[i*12]:
        stack.append((be(F, i*12+8), p + '/'))
    elif p.startswith('data/') and p.endswith('.bin'):
        open(os.path.join(out, p[5:]), 'wb').write(read(be(F, i*12+4), be(F, i*12+8)))
