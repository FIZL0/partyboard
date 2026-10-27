#!/usr/bin/env python3
"""split.py LOG OUTPREFIX: write each session (a run of lines starting at frame 0) to OUTPREFIX.<n>"""
import sys
segs=[]
for line in open(sys.argv[1]):
    if line.startswith("0 "):
        segs.append([])
    if segs:
        segs[-1].append(line)
for i,s in enumerate(segs):
    open(f"{sys.argv[2]}.{i}","w").writelines(s)
    print(i, len(s), s[0].split()[2] if s else "")
