import sys
# end.script: mash-a.script before HEAD, then every port taps A every 20 frames; port 0 taps START alone in the windows
head, end = int(sys.argv[1]), int(sys.argv[2])
wins = [tuple(map(int, w.split('-'))) for w in sys.argv[3:]]
out = []
for line in open('/home/ryha/repos/partyboard-slots/s2/tools/netplay/mash-a.script'):
    p = line.split()
    if not p or p[0].startswith('#') or p[0] == 'end': continue
    if p[0] == 'seed' or int(p[0]) < head: out.append(line.strip())
out.append(f'end {end}')
for f in range(head, end, 20):
    st = any(lo <= f < hi for lo, hi in wins)
    for p in range(4):
        b = ('START' if p == 0 else 'none') if st else 'A'
        out.append(f'{f} {p} {b}')
    out += [f'{f+5} {p} none' for p in range(4)]
print('\n'.join(out))
