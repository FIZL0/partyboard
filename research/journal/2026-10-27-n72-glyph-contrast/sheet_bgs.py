#!/usr/bin/env python3
# sheet_bgs.py <sheet.raw> <out.png>: the N49 glyph sheet (sheet.c dump) composited on four backgrounds, top to
# bottom: N49 blue, modesel-like navy, board-bar near-black, light. Shown at 2x.
import struct, sys
from PIL import Image
raw = open(sys.argv[1], "rb").read()
w, h = struct.unpack("<II", raw[:8])
im = Image.frombytes("RGBA", (w, h), raw[8:])
bgs = [(70,110,170),(20,30,80),(30,30,35),(235,235,225)]
out = Image.new("RGBA", (w, h*len(bgs)))
for i,c in enumerate(bgs):
    bg = Image.new("RGBA", (w, h), c+(255,)); bg.alpha_composite(im); out.paste(bg,(0,h*i))
out = out.resize((out.width*2,out.height*2), Image.NEAREST)
out.save(sys.argv[2])
