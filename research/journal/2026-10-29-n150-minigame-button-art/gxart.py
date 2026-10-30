# N150: dump every sprite (ANM) and model texture (HSF) of decoded data files to PNGs + one contact sheet per archive.
# gxart.py <raw dir (unpack output, one subdir per archive)> <out dir> [archive ...]
import os, struct, sys
import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gxart_lib import *  # GX decoders, ANM_FMT, HSF_FMT

def anm_images(b):
    bank, pat, bmpn = s16(b, 0), s16(b, 2), s16(b, 4) & 0x7FFF
    bo = u32(b, 16)
    if not (0 < bmpn < 512 and 0 < bank < 1024 and 0 < pat < 4096 and bo + bmpn * 20 <= len(b)):
        return None
    if u32(b, 8) >= len(b) or u32(b, 12) >= len(b) or u32(b, 8) < 20:
        return None
    out = []
    for i in range(bmpn):
        o = bo + i * 20
        pix, fmt = b[o], b[o + 1]
        paln, w, h = s16(b, o + 2), s16(b, o + 4), s16(b, o + 6)
        palo, do = u32(b, o + 12), u32(b, o + 16)
        f = ANM_FMT.get(fmt & 15)
        if not f or not (0 < w <= 1024 and 0 < h <= 1024) or do >= len(b):
            return None
        pal = None
        if f in ('C4', 'C8'):
            pal = np.frombuffer(b[palo:palo + paln * 2], '>u2')
        out.append((i, f, w, h, decode(f, b[do:], w, h, pal, 'RGB5A3')))
    return out

def hsf_images(b):
    sec = lambda k: (u32(b, 8 + k * 8), u32(b, 12 + k * 8))
    bmo, bmc = sec(9); pao, pac = sec(10); sto, stc = sec(20)
    pals = []
    pbase = pao + pac * 16
    for i in range(pac):
        o = pao + i * 16
        pals.append((u32(b, o + 8), pbase + u32(b, o + 12)))
    out = []
    dbase = bmo + bmc * 32
    for i in range(bmc):
        o = bmo + i * 32
        name_ofs = u32(b, o)
        name = ''
        if name_ofs != 0xFFFFFFFF and sto + name_ofs < len(b):
            e = b.index(b'\0', sto + name_ofs); name = b[sto + name_ofs:e].decode('latin1')
        fmt, pix = b[o + 8], b[o + 9]
        w, h, pals_n = s16(b, o + 10), s16(b, o + 12), s16(b, o + 14)
        palidx, do = u32(b, o + 20), dbase + u32(b, o + 28)
        if fmt in HSF_FMT:
            f = HSF_FMT[fmt]; img = decode(f, b[do:], w, h)
            if fmt in (0, 1, 2, 3):  # tinted intensity: show as grey
                pass
        elif fmt in (9, 10, 11):
            f = 'C4' if pix < 8 else 'C8'
            pal = None
            if palidx < len(pals):
                cnt, po = pals[palidx]
                pal = np.frombuffer(b[po:po + cnt * 2], '>u2')
            img = decode(f, b[do:], w, h, pal, {9: 'RGB565', 10: 'RGB5A3', 11: 'IA8'}[fmt])
        else:
            continue
        out.append((i, f'{name}:{f}', w, h, img))
    return out

def main():
    raw, outd = sys.argv[1], sys.argv[2]
    arcs = sys.argv[3:] or sorted(os.listdir(raw))
    for arc in arcs:
        tiles = []
        for fn in sorted(os.listdir(os.path.join(raw, arc))):
            b = open(os.path.join(raw, arc, fn), 'rb').read()
            num = int(fn[:3])
            try:
                if b[:4] == b'HSFV':
                    imgs, kind = hsf_images(b), 'h'
                else:
                    imgs, kind = anm_images(b), 'a'
            except Exception as e:
                imgs = None
            if not imgs:
                continue
            for i, f, w, h, img in imgs:
                im = Image.fromarray(np.ascontiguousarray(img), 'RGBA')
                os.makedirs(os.path.join(outd, arc), exist_ok=True)
                tag = f'{num:03d}{kind}{i:02d}'
                im.save(os.path.join(outd, arc, tag + '.png'))
                tiles.append((tag, f, im))
        if tiles:
            sheet(tiles, os.path.join(outd, arc + '.png'))

def sheet(tiles, path, cell=96, cols=12):
    rows = (len(tiles) + cols - 1) // cols
    S = Image.new('RGB', (cols * cell, rows * (cell + 12)), (90, 110, 90))
    d = ImageDraw.Draw(S)
    for k, (tag, f, im) in enumerate(tiles):
        x, y = (k % cols) * cell, (k // cols) * (cell + 12)
        t = im.copy(); t.thumbnail((cell - 4, cell - 4))
        S.paste(t, (x + 2, y + 2), t)
        d.text((x + 2, y + cell), tag, fill=(255, 255, 0))
    S.save(path)

main()
