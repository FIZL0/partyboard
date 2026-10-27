#!/usr/bin/env python3
"""Prototype vs definition: param count and void/non-void return, per REL (with its include/REL header) + game."""
import os, re, sys
sys.path.insert(0, os.path.dirname(__file__))
root = sys.argv[1]
from argcheck_lib import strip, strip_pp, nargs, paren
sig = re.compile(r'(?m)^[ \t]*((?:extern\s+|static\s+|inline\s+)*)([A-Za-z_][\w \t\*]*?)\b([A-Za-z_]\w*)\s*\(')
KW = {'if','while','for','switch','return','sizeof','do','else','case','typedef'}
def collect(files):
    defs = {}; protos = []
    for f in files:
        try: s = strip_pp(strip(open(f, errors='replace').read()))
        except: continue
        for m in sig.finditer(s):
            ret = m.group(2).strip(); name = m.group(3)
            if name in KW or ret.split()[0] in KW or 'typedef' in ret or '(' in ret: continue
            if ret in ('return', 'else'): continue
            i = m.end() - 1; j = paren(s, i)
            if j < 0: continue
            k = j + 1
            while k < len(s) and s[k] in ' \t\n': k += 1
            inner = s[i+1:j]
            if '...' in inner: continue
            isvoid = re.fullmatch(r'(const\s+)?void', ret.replace('static','').replace('extern','').strip()) is not None
            line = s.count('\n', 0, m.start()) + 1
            if k < len(s) and s[k] == '{':
                defs.setdefault(name, (nargs(inner), isvoid, inner.strip() == '', f, line))
            elif k < len(s) and s[k] == ';':
                protos.append((name, nargs(inner), isvoid, inner.strip() == '', f, line))
    return defs, protos
def files_in(d, ext=('.c', '.h')):
    out = []
    for dp, _, fs in os.walk(d):
        out += [os.path.join(dp, x) for x in fs if x.endswith(ext)]
    return out
gfiles = files_in(os.path.join(root, 'src/game'), ('.c',)) + files_in(os.path.join(root, 'src/msm'), ('.c',))
ghdr = [f for f in files_in(os.path.join(root, 'include')) if '/REL/' not in f]
gdefs, gprotos = collect(gfiles + ghdr)
def check(defs, protos):
    for name, n, v, empty, f, line in protos:
        if name not in defs: continue
        dn, dv, dempty, df, dl = defs[name]
        msg = []
        if not empty and n != dn: msg.append(f"params {n} vs def {dn}")
        if v != dv: msg.append(f"void-return {v} vs def {dv}")
        if msg: print(f"{f}:{line}: {name}: {', '.join(msg)} ({df}:{dl})")
check(gdefs, gprotos)
reld = os.path.join(root, 'src/REL')
for r in sorted(os.listdir(reld)):
    fs = files_in(os.path.join(reld, r))
    hdrs = [os.path.join(root, 'include/REL', x) for x in os.listdir(os.path.join(root, 'include/REL')) if x.lower().startswith(r.lower())]
    hdrs += files_in(os.path.join(root, 'include/REL', r))
    defs, protos = collect(fs + hdrs)
    merged = dict(gdefs); merged.update(defs)
    check(merged, protos)
