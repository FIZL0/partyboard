# N150: format, size and FNV-1a 64 hash (of the GX texel bytes) of chosen sprite/model bitmaps, for button_art.cpp's
# table. arthash.py <raw dir> <archive>/<file>[:<bmp>] ...
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gxart_lib import size_of, ANM_FMT, HSF_FMT

def fnv(b):
    h = 0xCBF29CE484222325
    for x in b:
        h = ((h ^ x) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h

def bitmaps(b):
    if b[:4] == b'HSFV':
        bmo, bmc = struct.unpack_from('>II', b, 8 + 9 * 8)
        dbase = bmo + bmc * 32
        for i in range(bmc):
            o = bmo + i * 32
            fmt, pix = b[o + 8], b[o + 9]
            w, h = struct.unpack_from('>hh', b, o + 10)
            f = HSF_FMT.get(fmt) or ('C4' if pix < 8 else 'C8')
            yield i, 'hsf', fmt, f, w, h, dbase + struct.unpack_from('>I', b, o + 28)[0]
    else:
        n = struct.unpack_from('>h', b, 4)[0] & 0x7FFF
        bo = struct.unpack_from('>I', b, 16)[0]
        for i in range(n):
            o = bo + i * 20
            fmt = b[o + 1]
            w, h = struct.unpack_from('>hh', b, o + 4)
            yield i, 'anm', fmt, ANM_FMT[fmt & 15], w, h, struct.unpack_from('>I', b, o + 16)[0]

raw = sys.argv[1]
for spec in sys.argv[2:]:
    path, _, want = spec.partition(':')
    b = open(os.path.join(raw, path + '.dat'), 'rb').read()
    for i, kind, fmt, f, w, h, do in bitmaps(b):
        if want and int(want) != i:
            continue
        n = size_of(f, w, h)
        print(f'{path}:{i} {kind} fmt={fmt}({f}) {w}x{h} bytes={n} hash=0x{fnv(b[do:do + n]):016X}ULL')
