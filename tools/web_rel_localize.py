#!/usr/bin/env python3
"""N87: turn one REL's prelinked wasm object (`emcc -r` of its .o files) into something the web build can link
statically next to every other REL.

On the desktop each REL is a .so whose only export is ObjectSetup (rel.map); the RELs share many names
(lbl_1_data_0, ...), and dlopen gives each load fresh .data/.bss. In the browser there is no dlopen, so:

- every symbol the object defines becomes local, except KEEP (the REL's ObjectSetup_<name>, renamed by a -D),
  so the RELs no longer clash;
- its writable data segments (.data.*, .bss.*) are renamed SEGMENT, so wasm-ld lays them out as one output
  segment with __start_SEGMENT/__stop_SEGMENT, which src/port/web_rel.c snapshots at boot and copies back each
  time the REL is linked again (a dlopen's fresh data).

Usage: web_rel_localize.py IN.o OUT.o SEGMENT KEEP
"""
import sys

SYMTAB, SEGMENT_INFO = 8, 5
UNDEFINED, BINDING_WEAK, BINDING_LOCAL, EXPLICIT_NAME = 0x10, 0x1, 0x2, 0x40
FUNCTION, DATA, GLOBAL, SECTION, TAG, TABLE = range(6)


def leb(b, i):
    r = shift = 0
    while True:
        c = b[i]
        i += 1
        r |= (c & 0x7F) << shift
        shift += 7
        if c < 0x80:
            return r, i


def uleb(v):
    out = bytearray()
    while True:
        c = v & 0x7F
        v >>= 7
        out.append(c | (0x80 if v else 0))
        if not v:
            return bytes(out)


def name(b, i):
    n, i = leb(b, i)
    return b[i:i + n].decode(), i + n


def enc_name(s):
    s = s.encode()
    return uleb(len(s)) + s


def rewrite_symtab(p, keep):
    count, i = leb(p, 0)
    out = bytearray(uleb(count))
    changed = 0
    for _ in range(count):
        kind = p[i]
        i += 1
        flags, i = leb(p, i)
        body_start = i
        sym = None
        if kind in (FUNCTION, GLOBAL, TAG, TABLE):
            _, i = leb(p, i)
            if not flags & UNDEFINED or flags & EXPLICIT_NAME:
                sym, i = name(p, i)
        elif kind == DATA:
            sym, i = name(p, i)
            if not flags & UNDEFINED:
                for _ in range(3):
                    _, i = leb(p, i)
        elif kind == SECTION:
            _, i = leb(p, i)
        else:
            sys.exit(f"web_rel_localize: unknown symbol kind {kind}")
        if kind in (FUNCTION, DATA) and not flags & UNDEFINED and not flags & BINDING_LOCAL and sym != keep:
            flags = (flags & ~BINDING_WEAK) | BINDING_LOCAL
            changed += 1
        out += bytes([kind]) + uleb(flags) + p[body_start:i]
    return bytes(out), changed


def rewrite_segments(p, segment):
    count, i = leb(p, 0)
    out = bytearray(uleb(count))
    renamed = 0
    for _ in range(count):
        seg, i = name(p, i)
        align, i = leb(p, i)
        flags, i = leb(p, i)
        if seg == ".data" or seg.startswith(".data.") or seg == ".bss" or seg.startswith(".bss."):
            seg = segment
            renamed += 1
        out += enc_name(seg) + uleb(align) + uleb(flags)
    return bytes(out), renamed


def main():
    src, dst, segment, keep = sys.argv[1:5]
    b = open(src, "rb").read()
    if b[:4] != b"\0asm":
        sys.exit(f"web_rel_localize: {src} is not a wasm object")
    out = bytearray(b[:8])
    i = 8
    found = False
    while i < len(b):
        sid = b[i]
        size, j = leb(b, i + 1)
        payload = b[j:j + size]
        i = j + size
        if sid == 0:
            sec, k = name(payload, 0)
            if sec == "linking":
                found = True
                version, k = leb(payload, k)
                body = bytearray(enc_name(sec) + uleb(version))
                while k < len(payload):
                    sub = payload[k]
                    n, k = leb(payload, k + 1)
                    data = payload[k:k + n]
                    k += n
                    if sub == SYMTAB:
                        data, changed = rewrite_symtab(data, keep)
                    elif sub == SEGMENT_INFO:
                        data, renamed = rewrite_segments(data, segment)
                    body += bytes([sub]) + uleb(len(data)) + data
                payload = bytes(body)
        out += bytes([sid]) + uleb(len(payload)) + payload
    if not found:
        sys.exit(f"web_rel_localize: {src} has no linking section (not a relocatable object?)")
    open(dst, "wb").write(out)


if __name__ == "__main__":
    main()
