#!/usr/bin/env python3
"""Renders every family's glyphs (pb_button_icons_dump_sheet) to a PNG.

    cc sheet.c -o /tmp/sheet -Lbuild/linux-default-relwithdebinfo -ldol \
       -Wl,-rpath,$PWD/build/linux-default-relwithdebinfo -Wl,--unresolved-symbols=ignore-all
    /tmp/sheet /tmp/sheet.raw && python3 sheet.py /tmp/sheet.raw sheet.png

Rows: PlayStation, Xbox, Switch, Generic, Steam Deck. Columns: the Ctl enum in src/port/button_icons.cpp
(south, east, west, north, back, guide, start, L3, R3, LB, RB, d-pad up/down/left/right, LT, RT, left stick,
right stick, d-pad, other).
"""
import struct
import sys

from PIL import Image

raw = open(sys.argv[1], "rb").read()
w, h = struct.unpack("<II", raw[:8])
im = Image.frombytes("RGBA", (w, h), raw[8:])
bg = Image.new("RGBA", (w, h), (70, 110, 170, 255))
bg.alpha_composite(im)
bg.save(sys.argv[2])
