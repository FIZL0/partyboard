#!/usr/bin/env python3
# tapa.py > tapa.script: all four ports tap A every 20 frames (5 down) from frame 120 to 60000.
print("seed 0x50424E54")
print("end 60000")
for f in range(120, 60000, 20):
    for p in range(4):
        print(f, p, "A")
    for p in range(4):
        print(f + 5, p, "none")
