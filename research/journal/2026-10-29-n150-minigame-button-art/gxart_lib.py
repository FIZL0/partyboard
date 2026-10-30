# N150: GX texture decoders shared by gxart.py and arthash.py
import os, struct, sys
import numpy as np
from PIL import Image, ImageDraw

def u16(b, o): return struct.unpack_from('>H', b, o)[0]
def s16(b, o): return struct.unpack_from('>h', b, o)[0]
def u32(b, o): return struct.unpack_from('>I', b, o)[0]

def rgb5a3(v):
    v = v.astype(np.uint32)
    out = np.zeros(v.shape + (4,), np.uint8)
    hi = (v & 0x8000) != 0
    r = np.where(hi, ((v >> 10) & 31) * 255 // 31, ((v >> 8) & 15) * 17)
    g = np.where(hi, ((v >> 5) & 31) * 255 // 31, ((v >> 4) & 15) * 17)
    bb = np.where(hi, (v & 31) * 255 // 31, (v & 15) * 17)
    a = np.where(hi, 255, ((v >> 12) & 7) * 255 // 7)
    out[..., 0], out[..., 1], out[..., 2], out[..., 3] = r, g, bb, a
    return out

def rgb565(v):
    v = v.astype(np.uint32)
    out = np.full(v.shape + (4,), 255, np.uint8)
    out[..., 0] = ((v >> 11) & 31) * 255 // 31
    out[..., 1] = ((v >> 5) & 63) * 255 // 63
    out[..., 2] = (v & 31) * 255 // 31
    return out

def ia8(v):
    v = v.astype(np.uint32)
    out = np.zeros(v.shape + (4,), np.uint8)
    i = v & 255
    out[..., 0] = out[..., 1] = out[..., 2] = i
    out[..., 3] = v >> 8
    return out

def untile(vals, w, h, tw, th):
    """vals: flat per-texel values in GX tile order -> (h, w) array."""
    bw, bh = (w + tw - 1) // tw, (h + th - 1) // th
    a = vals[: bw * bh * tw * th].reshape(bh, bw, th, tw).transpose(0, 2, 1, 3).reshape(bh * th, bw * tw)
    return a[:h, :w]

def size_of(fmt, w, h):
    tile = {'I4': (8, 8, 4), 'I8': (8, 4, 8), 'IA4': (8, 4, 8), 'IA8': (4, 4, 16), 'RGB565': (4, 4, 16),
            'RGB5A3': (4, 4, 16), 'RGBA8': (4, 4, 32), 'C4': (8, 8, 4), 'C8': (8, 4, 8), 'CMPR': (8, 8, 4)}[fmt]
    tw, th, bpp = tile
    return ((w + tw - 1) // tw) * tw * ((h + th - 1) // th) * th * bpp // 8

def decode(fmt, data, w, h, pal=None, palfmt='RGB5A3'):
    n = size_of(fmt, w, h)
    d = np.frombuffer(data[:n].ljust(n, b'\0'), np.uint8)
    if fmt in ('I4', 'C4'):
        v = np.empty(d.size * 2, np.uint8); v[0::2] = d >> 4; v[1::2] = d & 15
        v = untile(v, w, h, 8, 8)
        if fmt == 'I4':
            i = v * 17; return np.dstack([i, i, i, i])
        return lut(v, pal, palfmt)
    if fmt in ('I8', 'C8'):
        v = untile(d, w, h, 8, 4)
        if fmt == 'I8': return np.dstack([v, v, v, v])
        return lut(v, pal, palfmt)
    if fmt == 'IA4':
        v = untile(d, w, h, 8, 4)
        i = (v & 15) * 17; a = (v >> 4) * 17
        return np.dstack([i, i, i, a]).astype(np.uint8)
    if fmt in ('IA8', 'RGB565', 'RGB5A3'):
        v = untile(d.view('>u2'), w, h, 4, 4)
        return {'IA8': ia8, 'RGB565': rgb565, 'RGB5A3': rgb5a3}[fmt](v)
    if fmt == 'RGBA8':
        bw, bh = (w + 3) // 4, (h + 3) // 4
        blk = d[: bw * bh * 64].reshape(bh * bw, 2, 16, 2)
        ar, gb = blk[:, 0], blk[:, 1]
        px = np.stack([ar[..., 1], gb[..., 0], gb[..., 1], ar[..., 0]], -1)  # (blocks,16,4)
        px = px.reshape(bh, bw, 4, 4, 4).transpose(0, 2, 1, 3, 4).reshape(bh * 4, bw * 4, 4)
        return px[:h, :w]
    if fmt == 'CMPR':
        bw, bh = (w + 7) // 8, (h + 7) // 8
        out = np.zeros((bh * 8, bw * 8, 4), np.uint8)
        o = 0
        for by in range(bh):
            for bx in range(bw):
                for sb in range(4):
                    c0, c1 = struct.unpack_from('>HH', data, o); bits = u32(data, o + 4); o += 8
                    cols = rgb565(np.array([c0, c1])).astype(np.int32)
                    if c0 > c1:
                        c2 = (2 * cols[0] + cols[1]) // 3; c3 = (cols[0] + 2 * cols[1]) // 3
                    else:
                        c2 = (cols[0] + cols[1]) // 2; c3 = np.array([0, 0, 0, 0])
                    pal4 = np.array([cols[0], cols[1], c2, c3], np.uint8)
                    x0, y0 = bx * 8 + (sb & 1) * 4, by * 8 + (sb >> 1) * 4
                    for p in range(16):
                        out[y0 + p // 4, x0 + p % 4] = pal4[(bits >> (30 - 2 * p)) & 3]
        return out[:h, :w]
    raise ValueError(fmt)

def lut(idx, pal, palfmt):
    if pal is None or len(pal) == 0: pal, palfmt = np.arange(256, dtype=np.uint16) * 0x0101, "IA8"
    pal = np.asarray(pal)
    conv = {'RGB5A3': rgb5a3, 'RGB565': rgb565, 'IA8': ia8}[palfmt](pal)
    idx = np.minimum(idx, len(conv) - 1)
    return conv[idx]

ANM_FMT = {0: 'RGBA8', 1: 'RGB5A3', 2: 'RGB5A3', 3: 'C8', 4: 'C4', 5: 'IA8', 6: 'IA4', 7: 'I8', 8: 'I4', 9: 'I8', 10: 'CMPR'}
HSF_FMT = {0: 'I4', 1: 'I8', 2: 'IA4', 3: 'IA8', 4: 'RGB565', 5: 'RGB5A3', 6: 'RGBA8', 7: 'CMPR'}

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

