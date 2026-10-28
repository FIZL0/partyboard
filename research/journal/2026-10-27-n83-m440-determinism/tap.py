# N83: m440 tap script for the boot hook (harness.diff): A taps on all ports every 20 frames, 5 long, in the
# party's phase (party.script taps at multiples of 20; m440 started at party frame 76339, at hook frame 4).
out = open('tap.script', 'w')
out.write('end 8000\n')
for f in range(1, 8000):
    g = f - 4 + 76339
    if g % 20 == 0:
        for p in range(4): out.write(f"{f} {p} A\n")
    if g % 20 == 5:
        for p in range(4): out.write(f"{f} {p} none\n")
