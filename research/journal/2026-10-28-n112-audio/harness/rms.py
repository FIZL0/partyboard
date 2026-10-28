import sys, struct, math, array
path = sys.argv[1]; rate = int(sys.argv[2]) if len(sys.argv) > 2 else 44100; ch = 2
a = array.array('h'); a.frombytes(open(path, 'rb').read()[: (len(open(path,'rb').read())//2)*2])
step = rate * ch
for s in range(0, len(a), step):
    blk = a[s:s+step]
    if not blk: break
    rms = math.sqrt(sum(x*x for x in blk) / len(blk)); peak = max(abs(x) for x in blk)
    print(f"{s//step:4d}s rms={rms:8.1f} peak={peak}")
