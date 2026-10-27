#!/usr/bin/env python3
"""Compare two --hash-log files and report the first frame whose state hash differs.

    tools/netplay/hashdiff.py run1.log run2.log

Prints the first differing frame, which sections of the line differ (sys, plr0-3, cfg, frand, rand8,
brand, pad; see hash_log_line in src/port/netplay.c), the last frame that matched, and the overlay
(ovl=omcurovl) at both. Exit status: 0 identical, 1 diverged or different length, 2 bad input.
"""
import sys


def parse(path):
    rows = []
    with open(path) as f:
        for n, line in enumerate(f, 1):
            parts = line.split()
            if not parts:
                continue
            if len(parts) < 2:
                sys.exit(f"{path}:{n}: expected '<frame> <hash> ...'")
            fields = dict(p.split("=", 1) for p in parts[2:] if "=" in p)
            rows.append((int(parts[0]), parts[1], fields))
    return rows


def main(argv):
    if len(argv) != 3:
        sys.exit(f"usage: {argv[0]} A.log B.log")
    a, b = parse(argv[1]), parse(argv[2])
    last_ok = None
    for ra, rb in zip(a, b):
        if ra[0] != rb[0]:
            print(f"frame numbers out of step: {ra[0]} vs {rb[0]}")
            return 1
        if ra[1] != rb[1] or ra[2] != rb[2]:
            print(f"first differing frame: {ra[0]} (last match: {last_ok[0] if last_ok else 'none'})")
            print(f"  hash  {ra[1]}  vs  {rb[1]}")
            for k in sorted(set(ra[2]) | set(rb[2])):
                va, vb = ra[2].get(k), rb[2].get(k)
                if va != vb:
                    print(f"  {k:6} {va}  vs  {vb}")
            if last_ok:
                print(f"  ovl at last match: {last_ok[2].get('ovl', '?')}")
            return 1
        last_ok = ra
    if len(a) != len(b):
        print(f"identical for {min(len(a), len(b))} frames, then one log ends ({len(a)} vs {len(b)} lines)")
        return 1
    ovls = []
    for r in a:
        o = r[2].get("ovl")
        if o is not None and (not ovls or ovls[-1][1] != o):
            ovls.append((r[0], o))
    print(f"identical: {len(a)} frames")
    if ovls:
        print("overlays: " + ", ".join(f"{o}@{fr}" for fr, o in ovls))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
