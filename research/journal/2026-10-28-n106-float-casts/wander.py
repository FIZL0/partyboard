#!/usr/bin/env python3
"""N106: wander.py [END] > wander.script: random pads on all four ports (buttons + both sticks + triggers, a new state
every 6-30 frames, fixed seed) for float_cast_check.sh's FCO_OVL runs with FCO_HUMAN seats, so the minigames' human
paths (stick-to-float speed and angle math) run too. A START every so often would pause, so it is left out."""
import random
import sys

END = int(sys.argv[1]) if len(sys.argv) > 1 else 9000
BUTTONS = ["A", "B", "X", "Y", "Z", "L", "R", "UP", "DOWN", "LEFT", "RIGHT"]
rng = random.Random(0x4E313036)
out = ["seed 0x50424E54", f"end {END}"]
f = 60
while f < END:
    for p in range(4):
        b = "+".join(sorted(rng.sample(BUTTONS, rng.choice([0, 1, 1, 2])))) or "none"
        st = [rng.choice([-128, -127, -100, -60, -20, 0, 0, 20, 60, 100, 127]) for _ in range(4)]
        tr = [rng.choice([0, 0, 64, 255]) for _ in range(2)]
        out.append(f"{f} {p} {b} {' '.join(map(str, st + tr))}")
    f += rng.randint(6, 30)
print("\n".join(out))
