import sys, glob, collections
def load(p):
    sess=[]; last=None
    for l in open(p):
        f,rest=l.split(' ',1); f=int(f)
        if last is None or f<last: sess.append(collections.OrderedDict())
        last=f
        sess[-1].setdefault(f,[]).append(rest.strip())
    return sess
h=load(glob.glob('host/probe.*')[0]); g=load(glob.glob('guest/probe.*')[0])
print('sessions', [ (min(s),max(s)) for s in h], [(min(s),max(s)) for s in g])
hi=int(sys.argv[1]); gi=int(sys.argv[2])
H=h[hi]; G=g[gi]
n=0
for f in H:
    if f in G and H[f]!=G[f]:
        a=set(H[f]); b=set(G[f])
        print('diff frame',f); print(' host only',sorted(a-b)[:10]); print(' guest only',sorted(b-a)[:10]); n+=1
        if n>=int(sys.argv[3]): break
