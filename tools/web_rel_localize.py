#!/usr/bin/env python3
"""N87: turn one REL's prelinked wasm object (`emcc -r` of its .o files) into something the web build can link
statically next to every other REL.

On the desktop each REL is a .so whose only export is ObjectSetup (rel.map); the RELs share many names
(lbl_1_data_0, ...), and dlopen gives each load fresh .data/.bss. In the browser there is no dlopen, so:

- every symbol the object defines becomes local, except KEEP (the REL's ObjectSetup_<name>, renamed by a -D),
  so the RELs no longer clash;
- every function it defines but KEEP is named <rel>.<name> (N90), the REL's name taken from KEEP: the RELs'
  local functions (BoardCreate, fn_1_1548, ...) would repeat in the module, and wasm-dis tells repeated names apart
  only by their function index, which another link (WEB_DEBUG's) doesn't keep. tools/web_asyncify_list.py lists
  functions by name, and the browser's stacks say which REL a function is from;
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


def rewrite_symtab(p, keep, prefix, fn_names):
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
        body = p[body_start:i]
        if kind in (FUNCTION, DATA) and not flags & UNDEFINED and not flags & BINDING_LOCAL and sym != keep:
            flags = (flags & ~BINDING_WEAK) | BINDING_LOCAL
            changed += 1
        if kind == FUNCTION and not flags & UNDEFINED and sym is not None and sym != keep:
            index, j = leb(p, body_start)
            body = uleb(index) + enc_name(prefix + sym)
            fn_names[sym] = prefix + sym
        out += bytes([kind]) + uleb(flags) + body
    return bytes(out), changed


def rewrite_function_names(p, fn_names):
    """The name section's function names, which wasm-ld names the output's functions by: as the symbols."""
    sec, k = name(p, 0)
    out = bytearray(enc_name(sec))
    while k < len(p):
        sub = p[k]
        n, k = leb(p, k + 1)
        data = p[k:k + n]
        k += n
        if sub == 1:
            count, j = leb(data, 0)
            names = bytearray(uleb(count))
            for _ in range(count):
                index, j = leb(data, j)
                fn, j = name(data, j)
                names += uleb(index) + enc_name(fn_names.get(fn, fn))
            data = bytes(names)
        out += bytes([sub]) + uleb(len(data)) + data
    return bytes(out)


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
    fn_names = {}
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
                        data, changed = rewrite_symtab(data, keep, keep.removeprefix("ObjectSetup_") + ".", fn_names)
                    elif sub == SEGMENT_INFO:
                        data, renamed = rewrite_segments(data, segment)
                    body += bytes([sub]) + uleb(len(data)) + data
                payload = bytes(body)
            elif sec == "name":
                payload = rewrite_function_names(payload, fn_names)
        out += bytes([sid]) + uleb(len(payload)) + payload
    if not found:
        sys.exit(f"web_rel_localize: {src} has no linking section (not a relocatable object?)")
    open(dst, "wb").write(out)


if __name__ == "__main__":
    main()
