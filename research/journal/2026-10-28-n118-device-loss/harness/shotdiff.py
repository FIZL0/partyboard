#!/usr/bin/env python3
"""shotdiff.py DIR_A DIR_B [OFFSET_A:OFFSET_B ...]: compare the loopNNNNN.png shots of two runs pixel by pixel.

Pairs are the same names, or with explicit loop pairs "1501:1503" (a device loss skips one or more game ticks, so
the loop numbers of a run with losses run ahead of the game frames)."""
import os
import sys

from PIL import Image, ImageChops

a, b = sys.argv[1], sys.argv[2]
pairs = [tuple(int(x) for x in p.split(':')) for p in sys.argv[3:]]
if not pairs:
    pairs = [(int(f[4:9]),) * 2 for f in sorted(os.listdir(a)) if f.startswith('loop')]
for x, y in pairs:
    ia = Image.open(f'{a}/loop{x:05}.png').convert('RGB')
    ib = Image.open(f'{b}/loop{y:05}.png').convert('RGB')
    box = ImageChops.difference(ia, ib).getbbox()
    print(x, y, 'identical' if box is None else f'differs {box}')
