#!/usr/bin/env python3
"""N140: ciso2iso.py IN.ciso OUT.iso: a CISO (header 'CISO', block size u32 LE, 0x7ff8 one-byte 'block present' flags,
then the present blocks in order) as a plain disc image (absent blocks zero)."""
import sys

src, dst = sys.argv[1:3]
with open(src, "rb") as f, open(dst, "wb") as o:
    head = f.read(0x8000)
    assert head[:4] == b"CISO", "not a CISO"
    bs = int.from_bytes(head[4:8], "little")
    flags = head[8:0x8000]
    last = max(i for i, v in enumerate(flags) if v)
    for i in range(last + 1):
        o.write(f.read(bs) if flags[i] else bytes(bs))
    # a GameCube disc is 1459978240 bytes; the CISO drops the empty blocks at the end
    o.truncate(max(o.tell(), 1459978240))
