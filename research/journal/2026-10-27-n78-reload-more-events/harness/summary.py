#!/usr/bin/env python3
"""summary.py <outroot> <name>...: one line per run: reload frame/overlay, signal, board starts' heaps, the resumed
turn point, board overlays and minigames after it, and how the run ended (script end hash or the last line)."""
import os, re, sys

root = sys.argv[1]
for name in sys.argv[2:]:
    log = open(os.path.join(root, name, "log.txt"), errors="replace").read()
    ev = re.search(r"GDB: .* hit, frame (\d+)", log)
    rl = re.search(r"GDB: reload at frame (\d+), ovl (\d+), player (-?\d+)", log)
    sig = re.findall(r"Program received signal (\w+)", log)
    heaps = re.findall(r"board overlay \d+ starts at frame \d+, turn (\d+)/\d+, heaps (\w+)", log)
    tp = re.search(r"first turn point of epoch 2 at frame (\d+), player (-?\d+)", log)
    end = re.search(r"script ended at frame (\d+), hash (\w+)", log)
    after = log[rl.end():] if rl else ""
    mgs = len(re.findall(r"reseed at frame \d+: point 2", after))
    starts = len(re.findall(r"board overlay \d+ starts", after))
    print("%-22s ev %-6s reload %-12s sig %-8s heaps %-40s tp2 %-12s after: %d board starts, %d mg; %s" % (
        name, ev.group(1) if ev else "-", "%s/ovl%s/p%s" % rl.groups() if rl else "NONE", ",".join(sig) or "-",
        " ".join("t%s:%s" % h for h in heaps), "%s/p%s" % tp.groups() if tp else "NONE", starts, mgs,
        "end %s %s" % end.groups() if end else "NO END"))
