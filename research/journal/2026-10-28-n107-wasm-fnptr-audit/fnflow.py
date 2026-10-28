# N107: every place a C function's address leaves its own type, from clang's JSON AST of each src/*.c file of the
# web build (compdb from `ninja -C build/web-cmake -t compdb`, emsdk env loaded):
#   python3 fnflow.py cdb.json > flow.txt
# Lines (deduplicated, headers are seen by many files):
#   MISMATCH  file:line fn 'its type' -> 'pointer type' [wasm sigs]  (a cast/conversion to another wasm signature)
#   ESCAPE    file:line fn 'its type' -> 'void *' or integer, context  (where it goes: callee#arg, lhs, field)
#   SINK      file:line 'void *' -> 'pointer type', context           (where an untyped value becomes a fn pointer)
#   PCAST     file:line 'fn pointer' -> 'fn pointer' of another wasm signature, not from a function name
#   NOPROTO   file:line call through an unprototyped 'T (*)()' with arguments, their wasm types
# A wasm signature: params and result as i32/i64/f32/f64 (pointers, ints, enums, structs by value: i32; struct
# results: an sret i32 first parameter; '...': one i32). Only a signature difference traps (call_indirect checks it).
import json, re, shlex, subprocess, sys, concurrent.futures as cf

def cmd_of(e):
    a = shlex.split(e['command']); out = []; skip = False
    for x in a:
        if skip: skip = False; continue
        if x in ('-o', '-MF', '-MT', '-MQ'): skip = True; continue
        if x in ('-MD', '-MMD', '-c'): continue
        out.append(x)
    return out + ['-fsyntax-only', '-w', '-Xclang', '-ast-dump=json']

def split_top(s, sep=','):
    parts, depth, cur = [], 0, ''
    for c in s:
        if c in '([': depth += 1
        elif c in ')]': depth -= 1
        if c == sep and depth == 0: parts.append(cur); cur = ''
        else: cur += c
    if cur.strip(): parts.append(cur)
    return [p.strip() for p in parts]

class Types:
    def __init__(self): self.td = {}; self.records = set()
    def resolve(self, t, n=0):
        t = re.sub(r'\b(const|volatile|restrict|__restrict)\b', '', t).strip()
        if n > 20: return t
        m = re.fullmatch(r'(?:struct |union |enum )?([A-Za-z_]\w*)', t)
        if m and m.group(1) in self.td: return self.resolve(self.td[m.group(1)], n + 1)
        return t
    def is_fnptr(self, t):
        return self.fn_parts(t) is not None
    def fn_parts(self, t):
        """'R (*)(A, B)' or 'R (A, B)' (possibly via typedefs) -> (R, [A, B] or None if unprototyped, variadic)"""
        t = self.resolve(t)
        m = re.fullmatch(r'(.*?)\s*\((?:\*|\s*)\)\s*\((.*)\)', t) or re.fullmatch(r'(.*?)\s*\((.*)\)', t)
        if not m: return None
        ret, args = m.group(1).strip(), m.group(2).strip()
        if re.search(r'\(\*\)$', ret): return None
        if '(' in ret and not ret.endswith(')'): return None
        if args == '': return (ret, None, False)
        if args == 'void': return (ret, [], False)
        a = split_top(args)
        var = a and a[-1] == '...'
        return (ret, [x for x in a if x != '...'], var)
    def val(self, t):
        t = self.resolve(t)
        if '*' in t or '[' in t or '(' in t: return 'i32'
        if t == 'void': return None
        if t == 'float': return 'f32'
        if t == 'double' or t == 'long double': return 'f64' if t == 'double' else 'f128'
        if re.search(r'\blong long\b', t) or t in ('__int64', 'int64_t', 'uint64_t'): return 'i64'
        if t.startswith('struct ') or t.startswith('union ') or t in self.records: return 'rec'
        return 'i32'
    def wasm(self, t):
        p = self.fn_parts(t)
        if p is None: return None
        ret, args, var = p
        r = self.val(ret)
        params = []
        if r == 'rec': params.append('i32'); r = None
        if args is None: return ('?', r)
        for a in args:
            v = self.val(a)
            params.append('i32' if v == 'rec' else v)
        if var: params.append('i32')
        return ('(' + ','.join(params) + ')', r or 'void')

def fmt(w): return f'{w[0]}->{w[1]}' if w else '?'

def analyze(e):
    r = subprocess.run(cmd_of(e), cwd=e['directory'], capture_output=True)
    try: root = json.loads(r.stdout)
    except Exception: return [f'ERROR {e["file"]}: {r.stderr[-200:]!r}']
    T = Types(); out = []
    pos = {'file': '?', 'line': 0}
    def upd(loc):
        # clang writes a location's file/line only when they change from the last one written (in text order;
        # a macro location writes its spellingLoc, then its expansionLoc)
        if not loc: return
        for l in ([loc['spellingLoc'], loc['expansionLoc']] if 'spellingLoc' in loc else [loc]):
            if 'file' in l: pos['file'] = l['file']
            if 'line' in l: pos['line'] = l['line']
    def here(): return f"{pos['file'].replace('/home/ryha/repos/partyboard-slots/s2/', '')}:{pos['line']}"
    # pass 1: typedefs and records
    def p1(n):
        k = n.get('kind')
        if k == 'TypedefDecl':
            t = n.get('type', {}); T.td[n['name']] = t.get('desugaredQualType', t.get('qualType', ''))
            if t.get('qualType', '').startswith(('struct ', 'union ')): T.records.add(n['name'])
        if k == 'RecordDecl' and n.get('name'): T.records.add('struct ' + n['name'])
        for c in n.get('inner', []): p1(c)
    p1(root)
    def qt(n): t = n.get('type', {}); return t.get('desugaredQualType', t.get('qualType', ''))
    def name_of(n):
        if n is None: return '?'
        k = n.get('kind')
        if k == 'DeclRefExpr': return n.get('referencedDecl', {}).get('name', '?')
        if k == 'MemberExpr': return '.' + n.get('name', '?')
        if k in ('ImplicitCastExpr', 'CStyleCastExpr', 'ParenExpr', 'ArraySubscriptExpr', 'UnaryOperator'):
            return name_of(n.get('inner', [None])[0]) + ('[]' if k == 'ArraySubscriptExpr' else '')
        return k
    def context(stack, i):
        # stack[i] is the outermost cast; stack[i-1] its parent
        child = stack[i]; j = i - 1
        while j >= 0 and stack[j].get('kind') in ('ParenExpr', 'ConditionalOperator'):
            child = stack[j]; j -= 1
        if j < 0: return '?'
        p = stack[j]; k = p.get('kind')
        if k == 'CallExpr':
            inner = p.get('inner', [])
            idx = next((a for a, c in enumerate(inner) if c is child), -1)
            return f'arg{idx - 1} of {name_of(inner[0])}'
        if k == 'BinaryOperator': return f'{p.get("opcode")} {name_of(p["inner"][0])}'
        if k == 'VarDecl': return f'init {p.get("name")}'
        if k == 'InitListExpr':
            return f'init-list of {p.get("type", {}).get("qualType", "?")}'
        if k == 'ReturnStmt': return 'return'
        return k
    def walk(n, stack):
        upd(n.get('loc')); r_ = n.get('range', {}); upd(r_.get('begin')); upd(r_.get('end'))
        k = n.get('kind')
        if k == 'DeclRefExpr' and n.get('referencedDecl', {}).get('kind') == 'FunctionDecl':
            fn = n['referencedDecl']['name']; ftype = n['referencedDecl']['type']['qualType']
            # direct call?
            p = stack[-1] if stack else None
            gp = stack[-2] if len(stack) > 1 else None
            direct = (p and p.get('kind') == 'ImplicitCastExpr' and p.get('castKind') == 'FunctionToPointerDecay'
                      and gp and gp.get('kind') == 'CallExpr' and gp['inner'][0] is p)
            if not direct:
                out.append(f'ADDR     {here()} {fn} [{fmt(T.wasm(ftype))}] {e["file"]}')
                i = len(stack) - 1
                while i >= 0 and (stack[i].get('kind') in ('ImplicitCastExpr', 'CStyleCastExpr', 'ParenExpr') or
                                  (stack[i].get('kind') == 'UnaryOperator' and stack[i].get('opcode') == '&')):
                    i -= 1
                outer = stack[i + 1] if i + 1 < len(stack) else n
                tt = qt(outer)
                wf, wt = T.wasm(ftype), T.wasm(tt)
                ctx = context(stack, i + 1) if i + 1 < len(stack) else '?'
                if wt is not None and wt[0] == '?' and wf != wt:
                    out.append(f'UNPROTO  {here()} {fn} \'{ftype}\' [{fmt(wf)}] -> \'{tt}\' {ctx}')
                elif wt is not None:
                    if wf != wt:
                        out.append(f'MISMATCH {here()} {fn} \'{ftype}\' -> \'{tt}\' [{fmt(wf)} vs {fmt(wt)}] {ctx}')
                elif outer is not n:
                    out.append(f'ESCAPE   {here()} {fn} \'{ftype}\' [{fmt(wf)}] -> \'{tt}\' {ctx}')
        if k in ('ImplicitCastExpr', 'CStyleCastExpr') and n.get('castKind') not in ('FunctionToPointerDecay', 'LValueToRValue', 'NoOp'):
            tt = qt(n); inner = n.get('inner', [{}])[0]; st = qt(inner)
            if T.is_fnptr(tt):
                # find whether inner chain is a function name
                c = inner
                while c.get('kind') in ('ImplicitCastExpr', 'CStyleCastExpr', 'ParenExpr', 'UnaryOperator') and c.get('inner'):
                    c = c['inner'][0]
                fromfn = c.get('kind') == 'DeclRefExpr' and c.get('referencedDecl', {}).get('kind') == 'FunctionDecl'
                if not fromfn:
                    if not T.is_fnptr(st):
                        if not (st in ('int', 'long') and c.get('kind') == 'IntegerLiteral'):
                            out.append(f'SINK     {here()} \'{st}\' -> \'{tt}\' [{fmt(T.wasm(tt))}] {context(stack + [n], len(stack))} from {name_of(inner)}')
                    elif T.wasm(st) != T.wasm(tt):
                        out.append(f'PCAST    {here()} \'{st}\' [{fmt(T.wasm(st))}] -> \'{tt}\' [{fmt(T.wasm(tt))}] {context(stack + [n], len(stack))} from {name_of(inner)}')
        if k == 'FunctionDecl' and any(c.get('kind') == 'CompoundStmt' for c in n.get('inner', [])) \
                and pos['file'] == e['file']:
            ps = [c for c in n.get('inner', []) if c.get('kind') == 'ParmVarDecl']
            ret = n['type']['qualType'].split('(')[0].strip()
            sig = f"{ret} ({', '.join(qt(c) for c in ps) or 'void'})"
            out.append(f'DEF      {here()} {n["name"]} [{fmt(T.wasm(sig))}] {e["file"]}')
        if k == 'CallExpr':
            inner = n.get('inner', [])
            if inner:
                ct = qt(inner[0])
                c = inner[0]
                while c.get('kind') in ('ImplicitCastExpr', 'ParenExpr') and c.get('inner'): c = c['inner'][0]
                isfn = c.get('kind') == 'DeclRefExpr' and c.get('referencedDecl', {}).get('kind') == 'FunctionDecl'
                parts = T.fn_parts(ct)
                if parts and parts[1] is None and len(inner) > 1:
                    args = ','.join(T.val(qt(a)) or 'void' for a in inner[1:])
                    out.append(f'NOPROTO  {here()} {"direct" if isfn else "indirect"} {name_of(inner[0])} \'{ct}\' ({args})')
        stack.append(n)
        for c in n.get('inner', []):
            walk(c, stack)
        stack.pop()
    sys.setrecursionlimit(100000)
    walk(root, [])
    return out

if __name__ == '__main__':
    d = json.load(open(sys.argv[1]))
    filt = sys.argv[2] if len(sys.argv) > 2 else '/src/'
    ents = [e for e in d if filt in e['file'] and e['file'].endswith('.c')]
    seen = set()
    defs, addrs = {}, []
    mod = lambda f: f.split('/src/REL/')[1].split('/')[0] if '/src/REL/' in f else 'dol'
    with cf.ProcessPoolExecutor(4) as ex:
        for lines in ex.map(analyze, ents):
            for l in lines:
                w = l.split()
                if w[0] == 'DEF': defs.setdefault((mod(w[4]), w[2]), []).append((w[3], w[1])); continue
                if w[0] == 'ADDR': addrs.append(w); continue
                if l not in seen and '/build/' not in l.split()[1]:
                    seen.add(l); print(l, flush=True)
    # a function taken by address with a declaration whose wasm type isn't its definition's (another file's
    # prototype is wrong): wasm-ld only checks direct calls
    for w in addrs:
        for m in (mod(w[4]), 'dol'):
            d = defs.get((m, w[2]))
            if d:
                for sig, where in d:
                    if sig != w[3] and f'XDECL {w[1]} {w[2]}' not in seen:
                        seen.add(f'XDECL {w[1]} {w[2]}')
                        print(f'XDECL    {w[1]} {w[2]} declared {w[3]}, defined {sig} at {where}')
                break
