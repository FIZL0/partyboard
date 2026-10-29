#!/usr/bin/env python3
"""onehl.py out.log [from-to]: per traced frame (PB_TRACE), how many items look highlighted.

An item is highlighted when its computed look (background, border alpha, colour, opacity, transform, decorator, box-shadow) differs from its
resting look (the look it has most often while neither focus-visible nor hovered, apart for :selected, e.g. the
open tab, whose own look is not a focus highlight). Prints every frame that does not
show exactly one highlighted item, or whose highlighted item is not the focused one (the frame a key is pressed is
allowed one frame of lag: the trace is taken before that frame's RmlUi update).
"""
import re
import sys
from collections import Counter, defaultdict

log = open(sys.argv[1], errors="replace").read().splitlines()
lo, hi = (int(x) for x in sys.argv[2].split("-")) if len(sys.argv) > 2 else (0, 1 << 30)
item_re = re.compile(r" (button|tab|select-button)'([^']*)'\[([A-Z]*)\] (bg\w+ bd\w+ co\w+ op[\d.]+ tf\{[^}]*\}+(?: dc\{[^}]*\} sh\{[^}]*\})?) st(-?\d+);")
frames = []
keys = set()
for line in log:
    m = re.search(r"\] F(\d+) key ", line)
    if m:
        keys.add(int(m.group(1)))
        continue
    m = re.search(r"\] F(\d+) focus=<[^>]*>'([^']*)' \|(.*)$", line)
    if not m or not lo <= int(m.group(1)) <= hi:
        continue
    items = [(f"{t}:{name}:{st}", flags, look) for t, name, flags, look, st in item_re.findall(m.group(3))]
    frames.append((int(m.group(1)), m.group(2), items))

rest = defaultdict(Counter)
for _, _, items in frames:
    for key, flags, look in items:
        if "V" not in flags and "H" not in flags:
            rest[(key, "S" in flags)][look] += 1
bad = 0
for n, focus, items in frames:
    lit = []
    for key, flags, look in items:
        r = rest[(key, "S" in flags)]
        if (look != r.most_common(1)[0][0]) if r else ("V" in flags or "H" in flags):
            lit.append(key)  # never seen resting in this state: its flags decide
    lit_ok = len(lit) == 1 and any(k == lit[0] and ("V" in f or "H" in f) for k, f, _ in items)
    if not lit_ok and (n - 1) not in keys:
        bad += 1
        print(f"F{n}: focus '{focus}', {len(lit)} highlighted: {lit}")
print(f"{len(frames)} frames, {bad} without exactly one highlighted (focused) item, {len(keys)} key presses")
