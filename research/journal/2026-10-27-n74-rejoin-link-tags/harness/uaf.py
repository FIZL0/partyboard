import sys,re
# HSFDATA blocks of Hu3DModelLink (libdol+5f3fea) freed by Hu3DModelKill's FreeNum(mallocNoLink) (return addr in 5f611c..5f612a)
for p in sys.argv[1:]:
    live={}; n=0; links=0; stale=0
    for l in open(p, errors='replace'):
        x=l.split(); kind=x[2]; key=(x[3],x[5])
        bt=l.partition(':')[2].split()
        if kind=='A':
            live[key]=bt[0]
            if bt[0]=='libdol.so+5f3fea':
                links+=1
                if x[9].rstrip(':')!='0': stale+=1
        elif kind=='F':
            src=live.pop(key,None)
            if src=='libdol.so+5f3fea' and bt[0]!='libdol.so+5f611c' and any(re.match(r'libdol.so\+5f61(1[d-f]|2[0-9a])$',b) for b in bt):
                n+=1
                if n<=5: print(p, 'FreeNum hit a link HSFDATA:', l[:200])
    print(p, 'links', links, 'with stale num', stale, 'freed by another FreeNum', n)
