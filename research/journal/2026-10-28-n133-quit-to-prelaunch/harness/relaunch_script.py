#!/usr/bin/env python3
"""relaunch_script.py START_AT END < tools/netplay/party.script > out.script

party.script's pads (run it with --match 0@1500) until START_AT, then all four ports tap A and port 0 A+START every
40 frames until END: after a PB_RELAUNCH_TEST quit the rebooted bootDll's title needs START (party.script's A taps
alone stay on it)."""
import sys

start_at, end = int(sys.argv[1]), int(sys.argv[2])
for line in sys.stdin:
    w = line.split()
    if w and w[0] == "end":
        print(f"end {end}")
    elif w and w[0].isdigit():
        if int(w[0]) < start_at:
            print(line, end="")
    else:
        print(line, end="")
f = start_at
while f < end:
    for p in range(4):
        print(f"{f} {p} {'A+START' if p == 0 else 'A'}")
    for p in range(4):
        print(f"{f + 5} {p} none")
    f += 40
