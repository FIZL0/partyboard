# N82: rerun the -Warray-bounds=2 -Wstringop-overflow scan over src/game + src/REL with the build flags.
# From the repo root: (cd build/linux-default-relwithdebinfo && ninja -t compdb) > cc.json; python3 research/journal/<this>/warn.py cc.json [file substrings]
import json,subprocess,sys,os,re
from concurrent.futures import ThreadPoolExecutor
d=json.load(open(sys.argv[1]))
root=__import__('os').getcwd()+'/'
files=[e for e in d if e['file'].startswith(root+'src/game') or e['file'].startswith(root+'src/REL')]
files=[e for e in files if e['file'].endswith('.c')]
if len(sys.argv)>2: files=[e for e in files if any(f in e['file'] for f in sys.argv[2:])]
def run(e):
    cmd=e['command']
    cmd=re.sub(r' -o \S+',' -o /dev/null',cmd)
    cmd=re.sub(r' -M[DTF]( \S+)?','',cmd)
    cmd+=' -Warray-bounds=2 -Wstringop-overflow'
    r=subprocess.run(cmd,shell=True,cwd=e['directory'],capture_output=True,text=True)
    return [l.replace(root,'') for l in r.stderr.splitlines() if 'warning:' in l and ('array-bounds' in l or 'stringop-overflow' in l)]
with ThreadPoolExecutor(16) as ex:
    out=sorted(set(sum(ex.map(run,files),[])))
print('\n'.join(out)); print(len(out),file=sys.stderr)
