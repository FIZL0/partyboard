#!/usr/bin/env python3
"""N87: the functions Asyncify must leave alone in the web build, from `wasm-dis` of a probe link (no Asyncify,
names kept) on stdin; writes one ASYNCIFY_REMOVE entry per line to the file named by argv[1].

The web build links Rust (aurora's nod), whose std uses wasm exceptions, and so setjmp/longjmp is wasm's too
(cmake/web_early.cmake). Binaryen's Asyncify pass emits invalid code for a function with exception handling in
it, and no such function is ever on the stack when the game yields or switches fibers (they're Rust, libc++abi,
libpng/freetype error paths, the port's config/achievement loading), so they are not instrumented.

Names go in as they are, one per line (binaryen reads the file itself, see cmake/web.cmake, splitting on newlines
and pairing brackets): a wildcard costs binaryen a glob match against every function, which made the Asyncify pass
take minutes. Only a name wasm-dis numbered (a repeated one: the final link's module isn't numbered the same) or
one with unpaired brackets becomes a pattern.
"""
import re
import sys

EH = re.compile(r"\((try|try_table|throw|throw_ref|rethrow|delegate)\b")
# wasm-dis quotes a name with spaces in it: $"..."
FUNC = re.compile(r'^ \(func \$(?:"((?:[^"\\]|\\.)*)"|(\S+))')


def unescape(name):
    raw = re.sub(r"\\([0-9a-fA-F]{2}|.)", lambda m: chr(int(m.group(1), 16)) if len(m.group(1)) == 2 else m.group(1), name)
    return raw.encode("latin-1").decode("utf-8", "replace")


def balanced(s):
    stack = []
    pairs = {")": "(", ">": "<", "]": "[", "}": "{"}
    for c in s:
        if c in "(<[{":
            stack.append(c)
        elif c in pairs and (not stack or stack.pop() != pairs[c]):
            return False
    return not stack


def main():
    names = set()
    current = None
    for line in sys.stdin:
        m = FUNC.match(line)
        if m:
            current = m.group(1) if m.group(1) is not None else m.group(2)
            continue
        if current is not None and current not in names and EH.search(line):
            names.add(current)
    entries = set()
    for n in names:
        e = unescape(n)
        if "::" in e and re.search(r"_\d+$", e):
            e = re.sub(r"_\d+$", "*", e)
        if not balanced(e):
            e = re.sub(r"[()<>\[\]{}]", "*", e)
        entries.add(e)
    with open(sys.argv[1], "w") as f:
        for e in sorted(entries):
            f.write(e + "\n")
    wild = sum("*" in e for e in entries)
    print(f"web_eh_list: {len(names)} functions with exception handling, {len(entries)} entries ({wild} patterns)",
          file=sys.stderr)


if __name__ == "__main__":
    main()
