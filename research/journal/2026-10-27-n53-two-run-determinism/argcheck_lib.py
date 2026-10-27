import re
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
