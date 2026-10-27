#!/usr/bin/env python3
"""Find calls whose argument count differs from the callee's definition (per REL dir, plus src/game global)."""
import os, re, sys
root = sys.argv[1]
KW = {'if','while','for','switch','return','sizeof','do','else','case'}
def strip(s):
    s = re.sub(r'/\*.*?\*/', ' ', s, flags=re.S)
    s = re.sub(r'//[^\n]*', ' ', s)
    s = re.sub(r'"(\\.|[^"\\])*"', '""', s)
    s = re.sub(r"'(\\.|[^'\\])*'", "''", s)
    return s
def strip_pp(s):
    return '\n'.join('' if l.lstrip().startswith('#') else l for l in s.split('\n'))
def nargs(inner):
    inner = inner.strip()
    if inner == '' or inner == 'void': return 0
    depth = 0; n = 1
    for c in inner:
        if c in '([{': depth += 1
        elif c in ')]}': depth -= 1
        elif c == ',' and depth == 0: n += 1
    return n
def paren(s, i):
    # s[i] == '(' ; return index of matching ')'
    d = 0
    for j in range(i, len(s)):
        if s[j] == '(': d += 1
        elif s[j] == ')':
            d -= 1
            if d == 0: return j
    return -1
defre = re.compile(r'(?m)^[A-Za-z_][\w \t\*]*?\b([A-Za-z_]\w*)\s*\(')
callre = re.compile(r'\b([A-Za-z_]\w*)\s*\(')
def scan(files):
    defs = {}; calls = []
    for f in files:
        try: s = strip_pp(strip(open(f, errors='replace').read()))
        except: continue
        for m in defre.finditer(s):
            name = m.group(1); i = m.end() - 1; j = paren(s, i)
            if j < 0: continue
            k = j + 1
            while k < len(s) and s[k] in ' \t\n': k += 1
            if k < len(s) and s[k] == '{' and name not in KW:
                inner = s[i+1:j]
                if '...' in inner: defs[name] = None
                else: defs.setdefault(name, nargs(inner))
        for m in callre.finditer(s):
            name = m.group(1)
            if name in KW: continue
            i = m.end() - 1; j = paren(s, i)
            if j < 0: continue
            line = s.count('\n', 0, m.start()) + 1
            calls.append((name, nargs(s[i+1:j]), f, line, m.start()))
    return defs, calls
def report(defs, calls, label):
    seen = set()
    for name, n, f, line, pos in calls:
        d = defs.get(name, 'x')
        if d == 'x' or d is None: continue
        if n != d:
            key = (name, n, f, line)
            if key in seen: continue
            seen.add(key)
            print(f"{f}:{line}: {name} called with {n} args, defined with {d}")
game = []
for d in ['src/game', 'src/port', 'src/msm']:
    for dp, _, fs in os.walk(os.path.join(root, d)):
        game += [os.path.join(dp, x) for x in fs if x.endswith('.c')]
gdefs, gcalls = scan(game)
report(gdefs, gcalls, 'game')
reld = os.path.join(root, 'src/REL')
for r in sorted(os.listdir(reld)):
    fs = []
    for dp, _, xs in os.walk(os.path.join(reld, r)):
        fs += [os.path.join(dp, x) for x in xs if x.endswith('.c')]
    defs, calls = scan(fs)
    merged = dict(gdefs); merged.update(defs)
    report(merged, calls, r)
