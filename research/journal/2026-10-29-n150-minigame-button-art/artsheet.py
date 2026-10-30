# N150: artshot.sh's sheet: per picture a row of the original (gxart decode) and the redrawn families, 2x the original.
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from PIL import Image, ImageDraw
import gxart_lib as g

raw, tmp, out, items = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]

def original(path, idx):
    b = open(os.path.join(raw, path + '.dat'), 'rb').read()
    if b[:4] == b'HSFV':
        bmo, bmc = struct.unpack_from('>II', b, 8 + 72); pao, pac = struct.unpack_from('>II', b, 8 + 80)
        o = bmo + idx * 32; fmt, pix = b[o + 8], b[o + 9]; w, h = struct.unpack_from('>hh', b, o + 10)
        pi = struct.unpack_from('>I', b, o + 20)[0]; do = bmo + bmc * 32 + struct.unpack_from('>I', b, o + 28)[0]
        if fmt in g.HSF_FMT:
            return Image.fromarray(g.decode(g.HSF_FMT[fmt], b[do:], w, h), 'RGBA')
        cnt, po = struct.unpack_from('>II', b, pao + pi * 16 + 8)
        pal = np.frombuffer(b[pao + pac * 16 + po:pao + pac * 16 + po + cnt * 2], '>u2')
        return Image.fromarray(g.decode('C4' if pix < 8 else 'C8', b[do:], w, h, pal, {9: 'RGB565', 10: 'RGB5A3', 11: 'IA8'}[fmt]), 'RGBA')
    o = struct.unpack_from('>I', b, 16)[0] + idx * 20
    fmt = b[o + 1]; paln, w, h = struct.unpack_from('>hhh', b, o + 2); palo, do = struct.unpack_from('>II', b, o + 12)
    f = g.ANM_FMT[fmt & 15]
    pal = np.frombuffer(b[palo:palo + paln * 2], '>u2') if f in ('C4', 'C8') else None
    return Image.fromarray(np.ascontiguousarray(g.decode(f, b[do:], w, h, pal, 'RGB5A3')), 'RGBA')

rows = []
for it in items:
    path, idx = it.split(':')
    o = original(path, int(idx))
    tiles = [o.resize((o.size[0] * 2, o.size[1] * 2), Image.NEAREST)]
    for fam in (1, 2, 3, 4, 6):
        p = os.path.join(tmp, it.replace('/', '_').replace(':', '_') + f'-{fam}.rgba')
        d = open(p, 'rb').read(); w, h = struct.unpack_from('<II', d)
        im = Image.frombytes('RGBA', (w, h), d[8:])
        if im.size != tiles[0].size:
            im = im.resize(tiles[0].size, Image.BILINEAR)
        tiles.append(im)
    rows.append((it, tiles))
cw = max(t[0].size[0] for _, t in rows); W = 110 + 6 * (cw + 8)
H = sum(t[0].size[1] + 8 for _, t in rows)
S = Image.new('RGBA', (W, H), (60, 90, 60, 255)); d = ImageDraw.Draw(S); y = 0
for it, tiles in rows:
    d.text((2, y + 2), it, fill=(255, 255, 0, 255))
    for k, t in enumerate(tiles):
        S.alpha_composite(t, (110 + k * (cw + 8), y))
    y += tiles[0].size[1] + 8
S.save(out)
