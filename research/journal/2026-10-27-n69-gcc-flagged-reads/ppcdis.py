# N69: disassemble GC code with llvm-mc: ppcdis.py rel <x.rel> <text offset hex> <size hex> | dol <main.dol> <addr hex> <size hex>
# Offsets and sizes from config/GMPE01_00/[rels/<rel>/]symbols.txt. REL calls show as "bl .+0" (unrelocated).
import struct, sys, subprocess
be = lambda b, o: struct.unpack('>I', b[o:o+4])[0]
kind, path, addr, size = sys.argv[1], sys.argv[2], int(sys.argv[3], 16), int(sys.argv[4], 16)
b = open(path, 'rb').read()
if kind == 'rel':
    so = be(b, 0x10); off = be(b, so + 8) & ~1; base = 0
else:
    for i in range(7):
        a, s = be(b, 0x48+i*4), be(b, 0x90+i*4)
        if a <= addr < a+s: off = be(b, i*4); base = a
code = b[off + addr - base: off + addr - base + size]
txt = ' '.join('0x%02x' % c for c in code)
o = subprocess.run(['llvm-mc', '--disassemble', '-triple=powerpc'], input=txt, capture_output=True, text=True).stdout
pc = addr
for l in o.splitlines():
    l = l.strip()
    if not l or l.startswith('.text'): continue
    print('%08x  %s' % (pc, l)); pc += 4
