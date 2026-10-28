#!/usr/bin/env python3
"""adiff.py HOST.alloc GUEST.alloc [FROM]: compare the last epoch (frame numbers restart at a reload) of two
N74_LOG alloc logs from frame FROM (default 3: frame 0-2 are the old overlay's teardown and the scrub) and print the
first event (frame, overlay, alloc/free, heap, offset, size, num) that differs, with context. Caller chains are
module+offset (addr2line -e <module>)."""
import sys


def last_epoch(path):
    eps = [[]]
    last = -1
    for line in open(path, errors="replace"):
        fr = int(line.split()[0][1:])
        if fr < last - 10:
            eps.append([])
        last = fr
        eps[-1].append(line.rstrip("\n"))
    return eps[-1]


def key(line):
    return line.partition(":")[0]


start = int(sys.argv[3]) if len(sys.argv) > 3 else 3
A = [l for l in last_epoch(sys.argv[1]) if int(l.split()[0][1:]) >= start]
B = [l for l in last_epoch(sys.argv[2]) if int(l.split()[0][1:]) >= start]
print(len(A), len(B), "events")
for i, (a, b) in enumerate(zip(A, B)):
    if key(a) != key(b):
        for j in range(max(0, i - 5), min(i + 10, len(A), len(B))):
            print("=" if key(A[j]) == key(B[j]) else "!", "A", A[j][:260])
            print("   B", B[j][:260])
        break
else:
    print("identical over the common events")
