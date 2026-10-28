import json, shlex, subprocess, sys, concurrent.futures as cf
d = json.load(open(sys.argv[1]))
ents = [e for e in d if '/src/' in e['file'] and e['file'].endswith('.c')]
def run(e):
    a = shlex.split(e['command'])
    out = []; skip = False
    for x in a:
        if skip: skip = False; continue
        if x in ('-o', '-MF', '-MT', '-MQ'): skip = True; continue
        if x in ('-MD', '-MMD'): continue
        out.append(x)
    out += ['-fsyntax-only', '-Wcast-function-type-strict', '-Wincompatible-function-pointer-types']
    r = subprocess.run(out, cwd=e['directory'], capture_output=True, text=True)
    return [l for l in r.stderr.splitlines() if 'cast-function-type' in l or 'incompatible-function-pointer' in l or 'error' in l]
with cf.ThreadPoolExecutor(16) as ex:
    for lines in ex.map(run, ents):
        for l in lines: print(l)
