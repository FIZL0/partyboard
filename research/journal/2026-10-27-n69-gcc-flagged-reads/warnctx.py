# N69: print the enclosing function and the lines that mention the variable, per -Wmaybe-uninitialized warning:
# warnctx.py <gcc-warnings.txt> <path prefix>
import re, sys
# print the enclosing function for each warning, with lines mentioning the variable
lines = open(sys.argv[1]).read().splitlines()
sel = sys.argv[2]
for w in lines:
    m = re.match(r"(\S+?):(\d+):\d+: warning: '([^']+)' may be used", w)
    if not m or sel not in m.group(1): continue
    f, ln, var = m.group(1), int(m.group(2)), m.group(3).split('.')[0].split('[')[0]
    src = open(f, errors='replace').read().splitlines()
    s = ln - 1
    while s > 0 and not (src[s].startswith('{') or (re.match(r'^[A-Za-z].*\)\s*\{?\s*$', src[s]) and not src[s].startswith('#'))): s -= 1
    e = ln
    while e < len(src) and src[e] != '}': e += 1
    print('=====', w)
    print('%d: %s' % (s, src[s-1] if not src[s][0].isalpha() else src[s]))
    for i in range(s, e+1):
        if re.search(r'\b%s\b' % re.escape(var), src[i]) or i == ln - 1:
            print('%5d%s %s' % (i+1, '*' if i == ln-1 else ' ', src[i]))
