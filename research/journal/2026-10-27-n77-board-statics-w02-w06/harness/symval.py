#!/usr/bin/env python3
"""symval.py <libdol.so> <dump.bin> <symbol> [struct fmt, default 8h]: a libdol static's value in an n68_dump file."""
import struct, subprocess, sys

so, dump, name = sys.argv[1:4]
fmt = '<' + (sys.argv[4] if len(sys.argv) > 4 else '8h')
addr = None
for line in subprocess.run(['nm', '--defined-only', so], capture_output=True, text=True).stdout.splitlines():
    p = line.split()
    if len(p) == 3 and p[2] == name:
        addr = int(p[0], 16)
with open(dump, 'rb') as f:
    while True:
        h = f.read(24)
        if len(h) < 24:
            sys.exit('%s not in a dumped segment' % name)
        base, vaddr, n = struct.unpack('<QQQ', h)
        data = f.read(n)
        if vaddr <= addr < vaddr + n:
            off = addr - vaddr
            print(name, struct.unpack_from(fmt, data, off))
            break
