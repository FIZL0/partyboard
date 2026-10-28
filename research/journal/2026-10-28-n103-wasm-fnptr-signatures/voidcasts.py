import re, subprocess, os, sys
out = subprocess.run(['grep','-rnE',r'\((void|u32|uintptr_t|s32) ?\*?\) ?&?fn_[0-9A-Za-z_]+','src/REL','src/game'],capture_output=True,text=True).stdout
for line in out.splitlines():
    path, ln, code = line.split(':',2)
    for m in re.finditer(r'\((?:void|u32|uintptr_t|s32) ?\*?\) ?&?(fn_[0-9A-Za-z_]+)', code):
        fn = m.group(1)
        pre = code[:m.start()]
        callee = re.findall(r'([A-Za-z_][A-Za-z0-9_]*)\s*\(', pre)
        callee = callee[-1] if callee else ('=' if '=' in pre else '?')
        d = os.path.dirname(path)
        sig = subprocess.run(['grep','-rhE',r'^(static )?[A-Za-z0-9_ \*]+\b'+fn+r'\s*\([^;]*$', d],capture_output=True,text=True).stdout.strip().splitlines()
        print(f'{callee:28} {fn:14} {(sig[0] if sig else "?")[:70]:70} {path}:{ln}')
