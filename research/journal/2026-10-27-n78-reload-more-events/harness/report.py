#!/usr/bin/env python3
"""report.py <outroot> <base root> <name>...: per run, the reload, crashes, board starts, the statics the reload left
(N66 stale.py: pre2 vs start2) and the handle differences at the first turn point after it (turn2) against the
same board's fresh turn points (<base root>/fresh<board>/dump.txt tp1.., force-dumpall.sh; tp1 is turn 1 player 1's,
the first with no board intro before it). A static counts if it differs from it at every fresh turn point."""
import os, re, sys

PTR = re.compile(r"0x[0-9a-f]{9,}( <[^>]*>)?")
# A static that names a model, motion, sprite (group), window, effect, particle manager, anim, object or process.
HANDLE = re.compile(r"(mdl|model|mot|spr|grp|group|win|obj|proc|process|anim|par|eff|man|fx|ptr|id)", re.I)

# Differ after every cont reload, also one at a plain turn point: a fresh board keeps its intro's last values (start.c
# hostMdl 8, the logo's group member 67, ui.c's "your turn" member 65), a resumed board skips the intro and has the
# declaration values. Each is written before every read (start.c:244/842, ui.c:881); winPosIdx at every window create.
BENIGN = {"start.c::hostMdl", "start.c::logoSpr", "ui.c::yourTurnSpr", "window.c::winPosIdx"}

def load(path, tag):
    d = {}
    for line in open(path, errors="replace"):
        t, rest = line.rstrip("\n").split(" ", 1)
        if t == tag:
            k, v = rest.split(" = ", 1)
            d[k] = v
    return d

def mask(v):
    return PTR.sub("PTR", v)

def handle_diff(k, a, b):
    """True if a differs from b in something that is a handle: a pointer on one side only, or a handle-named static."""
    ma, mb = mask(a), mask(b)
    if ma == mb:
        return False
    if ma.count("PTR") != mb.count("PTR") or ma.count("0x0") != mb.count("0x0"):
        return True
    return bool(HANDLE.search(k.split("::")[1]))

root, base = sys.argv[1], sys.argv[2]
for name in sys.argv[3:]:
    d = os.path.join(root, name)
    log = open(os.path.join(d, "log.txt"), errors="replace").read()
    board = re.search(r"match start: board (\d)", log).group(1)
    print("== %s (board %s)" % (name, board))
    for l in log.splitlines():
        if l.startswith("GDB:") and "stopped" not in l or "board overlay" in l or "killed" in l and "item" in l \
                or "script ended" in l or "Program received" in l or "match over" in l:
            print("   " + l[:160])
    m = re.search(r"Program received signal.*\n((?:.*\n){0,8})", log)
    if m:
        print("   CRASH bt:\n" + m.group(1))
    dump = os.path.join(d, "dump.txt")
    if not os.path.exists(dump):
        print("   no dump")
        continue
    pre, post = load(dump, "pre2"), load(dump, "start2")
    if pre:
        print("   left by the reload: " + " ".join("%s=%s" % (k.replace(".c::", ":"), mask(pre[k])[:40])
                                                 for k in pre if pre[k] != post.get(k)))
    turn = load(dump, "turn2")
    fd = os.path.join(base, "fresh" + board, "dump.txt")
    refs = [r for r in (load(fd, "tp%d" % i) for i in range(1, 20)) if r]
    if not turn:
        print("   NO turn2 dump")
        continue
    hd = [k for k in refs[0] if k not in BENIGN and all(handle_diff(k, turn.get(k, ""), r[k]) for r in refs)]
    print("   handle diffs at turn2 vs %d fresh turn points: %d" % (len(refs), len(hd)))
    for k in hd:
        print("     %s\n       reload: %s\n       fresh:  %s" % (k, mask(turn.get(k, ""))[:200], mask(refs[0][k])[:200]))
