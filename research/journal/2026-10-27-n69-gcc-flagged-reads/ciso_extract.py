# N69: extract files and main.dol from a GC .ciso: ciso_extract.py <disc.ciso> <out dir> <name>...
import struct, sys
path, out = sys.argv[1], sys.argv[2]
f = open(path, 'rb')
hdr = f.read(0x8000)
assert hdr[:4] == b'CISO'
bs = struct.unpack('<I', hdr[4:8])[0]
m = hdr[8:]
idx = {}; n = 0
for i, b in enumerate(m):
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
h = read(0x420, 12); dol, fst, fsz = be(h,0), be(h,4), be(h,8)
F = read(fst, fsz); cnt = be(F, 8); strs = cnt * 12
def name(i): o = strs + (be(F, i*12) & 0xFFFFFF); return F[o:F.index(b'\0', o)].decode()
import os
want = sys.argv[3:]
for i in range(1, cnt):
    if F[i*12] == 0 and name(i) in want:
        open(os.path.join(out, name(i)), 'wb').write(read(be(F,i*12+4), be(F,i*12+8)))
        print(name(i))
# dol
d = read(dol, 0x100)
sz = max(be(d, i*4) + be(d, 0x90+i*4) for i in range(18))
open(os.path.join(out, 'main.dol'), 'wb').write(read(dol, sz))
