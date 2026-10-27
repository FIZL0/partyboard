#!/usr/bin/env python3
"""stale.py <dump> <N>: the statics N66's reset changes at board start N (preN vs startN), i.e. what the reload left."""
import re, sys
def load(path, tag):
    d = {}
    for line in open(path):
        t, rest = line.rstrip("\n").split(" ", 1)
        if t == tag:
            k, v = rest.split(" = ", 1)
            d[k] = v
    return d
pre, post = load(sys.argv[1], "pre" + sys.argv[2]), load(sys.argv[1], "start" + sys.argv[2])
print(" ".join("%s=%s" % (k, re.sub(r"0x[0-9a-f]{9,}( <[^>]*>)?", "PTR", pre[k])[:60]) for k in pre if pre[k] != post.get(k)))
