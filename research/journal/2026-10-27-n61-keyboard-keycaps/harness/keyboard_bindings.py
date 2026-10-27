import struct, sys
btn = [(13,0x100),(14,0x200),(24,0x400),(12,0x800),(40,0x1000),(20,0x10),(30,0x40),(8,0x20),(82,8),(81,4),(80,1),(79,2)]
ax = [(7,0),(4,1),(26,2),(22,3),(-1,4),(-1,5),(-1,6),(-1,7),(-1,8),(-1,9)]
out = struct.pack('<4sI', b'KBND', 3).ljust(32, b'\0')
for port in range(4):
    on = port == 0
    out += struct.pack('<B', 1 if on else 0)
    for sc, b in btn: out += struct.pack('<iHxx', sc if on else -1, b)
    for sc, a in ax: out += struct.pack('<iHh', sc if on else -1, a, 0)
open(sys.argv[1], 'wb').write(out)
