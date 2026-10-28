#!/usr/bin/env python3
"""longer.py END < tools/netplay/mash-a.script > out.script: mash-a's pads, then all four ports keep tapping A every
60 frames until END (time for a quit, the reboot and a Continue after mash-a's own end at 17400, on w01's turn 2)."""
import sys

end = int(sys.argv[1])
last = 0
for line in sys.stdin:
    w = line.split()
    if w and w[0] == "end":
        print(f"end {end}")
        continue
    if w and w[0].isdigit():
        last = max(last, int(w[0]))
    print(line, end="")
f = last + 25
while f < end:
    for p in range(4):
        print(f"{f} {p} A")
    for p in range(4):
        print(f"{f + 5} {p} none")
    f += 60
