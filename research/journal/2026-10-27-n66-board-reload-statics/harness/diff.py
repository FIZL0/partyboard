#!/usr/bin/env python3
"""diff.py <base dump> <tag> <other dump> <tag>: board statics that differ (heap/code addresses masked as PTR)."""
import re, sys
def load(path, tag):
    d = {}
    for line in open(path):
        t, rest = line.rstrip("\n").split(" ", 1)
        if t == tag:
            k, v = rest.split(" = ", 1)
            d[k] = re.sub(r"0x[0-9a-f]{9,}( <[^>]*>)?", "PTR", v)
    return d
a, b = load(sys.argv[1], sys.argv[2]), load(sys.argv[3], sys.argv[4])
for k in a:
    if a[k] != b.get(k):
        print("%s\n   %s: %s\n   %s: %s" % (k, sys.argv[2], a[k][:300], sys.argv[4], str(b.get(k))[:300]))
