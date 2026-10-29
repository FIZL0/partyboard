#!/usr/bin/env python3
"""N138: Create-IcoFromPng.ps1 for a Linux host (the MinGW cross build): the same sizes, from res/icon.png. Needs Pillow."""
import sys

from PIL import Image

src, out = sys.argv[1], sys.argv[2]
Image.open(src).convert("RGBA").save(out, sizes=[(s, s) for s in (16, 24, 32, 48, 64, 128, 256)])
