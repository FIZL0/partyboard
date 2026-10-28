# N129: print messages around <index> of bank <bank> of a mess/*.dat (MessData format): mess.py <file> <bank> <index>
import struct,sys
d=open(sys.argv[1],'rb').read()
be=lambda o:struct.unpack('>I',d[o:o+4])[0]
h=lambda o:struct.unpack('>H',d[o:o+2])[0]
nb=be(0);bo=be(4)
want=int(sys.argv[2],0); idx=int(sys.argv[3],0)
for i in range(nb):
    b=h(bo+i*4); k=h(bo+i*4+2)
    if b==want:
        bank=be(4+4*k); n=be(bank)
        for j in range(max(0,idx-2),min(n,idx+3)):
            o=bank+be(bank+4+4*j); e=d.index(b'\0',o)
            print(j, repr(d[o:e]))
