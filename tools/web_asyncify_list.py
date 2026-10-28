#!/usr/bin/env python3
"""N90: the functions Asyncify instruments in the web build (ASYNCIFY_ONLY), from `wasm-dis` of the probe link (no
Asyncify, names kept) on stdin and the probe's wasm-ld link map.

Usage: web_asyncify_list.py MAP OUT [REPORT] < probe.wat
       web_asyncify_list.py --check OUT JS < partyboard.wat   (after the link: every entry was instrumented, and
                                                               every async import of partyboard.js was a seed)

Binaryen's Asyncify instruments every function that can reach an import that unwinds (emscripten_sleep,
emscripten_fiber_swap, ...), and it takes every indirect call as one that can: most of the module (RmlUi, SDL,
sqlite, nod, freetype, libc++: all of it is reachable from some vtable or callback). This does the same analysis
with two refinements:

- A call_indirect only reaches table functions of its exact signature (wasm traps on any other), so it can unwind
  only if a table function of that signature can.
- Only the game's C (libdol.a's C objects: src/game, src/port's C, the decomp's SDK and musyx; the RELs) calls a
  function pointer that can lead to a yield: process entries, object and event callbacks. The C++ (the port's UI,
  aurora, RmlUi, fmt, libc++) and the C libraries (SDL, sqlite, freetype, libc) never yield from a callback: their
  indirect calls are taken as not unwinding, but SDL's main trampoline (LIBRARY_INDIRECT). The port's C++ yields
  only by direct calls (the frame limiter, the prelaunch loop's aurora_update), which are followed as everywhere.
  Template code the C++ shares (std::function, fmt) is counted where wasm-ld put it, so the C++ rule can't key on
  objects: it keys on the language.

Seeds are the imports Emscripten's JS library marks async (ASYNC_IMPORTS, emcc's ASYNCIFY_IMPORTS); a function can
unwind if it calls one that can (but NOT_TAKEN) or has a call_indirect that can (above), to a fixed point. The
result minus the functions with exception handling (tools/web_eh_list.py: Asyncify can't instrument them, and none
ever is on the stack when the game yields) is the list, plus what JS calls into to start a fiber
(Fibers.finishContextSwitch calls dynCall_vi, which exists only in the final link).

If a rule is wrong somewhere (a C++ callback that yields), that function's frame isn't saved: a debug build
(PB_WEB_DEBUG, Asyncify's asserts) traps there ("unreachable" right after the call), a release build goes wrong.
WEB_ASYNCIFY_INDIRECT=all in the environment of the build turns the language rule off (signatures only).
Checked (N90): the game's C pointers C++ calls are aurora's SDK callbacks (GX draw done, VI, DVD, card), interrupt
context on the GameCube, so they never wait; the one known way that would unwind through code left out is sqlite's
VFS (unixSync -> fsync, which always unwinds), and nothing opens a database in the browser build (aurora's caches
are off there).

Names: as tools/web_eh_list.py, verbatim. A name that repeats (wasm-dis tells the copies apart as `name_<index>`, by
the function's index in the wasm-ld output, which a WEB_DEBUG link doesn't keep) is listed as `name*`: every copy,
in any link. The RELs' functions don't repeat (tools/web_rel_localize.py names them <rel>.<name>); what's left is a
few static functions of the same name in two libraries. --check reads the final module.
"""
import os
import re
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(__file__))
from web_eh_list import EH, balanced, unescape  # noqa: E402

# The JS library functions with __async (Emscripten 6.0.13's src/lib/*.js, the emdawnwebgpu port), as wasm import
# names; --check compares them with the imports partyboard.js marks isAsync.
ASYNC_IMPORTS = {
    "emscripten_sleep", "emscripten_wget_data", "emscripten_scan_registers", "emscripten_fiber_swap",
    "_emval_await", "_dlopen_js", "emscripten_promise_await", "emscripten_promise_await_unchecked", "getaddrinfo",
    "getnameinfo", "emscripten_idb_load", "emscripten_idb_store", "emscripten_idb_delete", "emscripten_idb_exists",
    "emscripten_idb_clear", "__syscall_poll", "__syscall_epoll_pwait", "fd_sync", "_emscripten_dlsync_threads",
    # emdawnwebgpu's library_webgpu.js: wgpuInstanceWaitAny with a timeout (aurora's adapter and device requests)
    "emwgpuWaitAny",
}
ASYNC_PREFIXES = ("__asyncjs__", "_wasmfs_opfs_")
# JS calls these to enter wasm on a stack that unwinds later: a fiber's entry (dynCall_vi(entry, arg)).
JS_ENTRIES = ["dynCall_vi"]
# Library functions whose indirect calls are analyzed as the game's: SDL's main trampoline calls SDL_main
# (aurora_main -> the game) through a pointer.
LIBRARY_INDIRECT = {"SDL_RunApp", "SDL_CallMainFunction"}
# Direct calls that can't happen in this build (no threads): SDL's init/quit guards wait with SDL_Delay only while
# another thread is initializing the same subsystem. Every SDL_Init* passes them, so they would bring in ~600 SDL
# functions.
NOT_TAKEN = {("SDL_ShouldInit", "SDL_Delay"), ("SDL_ShouldQuit", "SDL_Delay")}


def game_code(obj):
    """True for an object of the game's C (the map's `In` column): libdol.a's C objects (src/game, src/port's C,
    the decomp's SDK and musyx), the RELs, the executable's own objects."""
    return ((obj.startswith("libdol.a(") and obj.endswith(".c.o)")) or obj.startswith("web-rel/")
            or obj.startswith("CMakeFiles/") or obj == "<internal>")


NAME = r'\$(?:"((?:[^"\\]|\\.)*)"|([^\s()]+))'
FUNC = re.compile(r"^ \(func " + NAME + r"(.*)$")
IMPORT = re.compile(r'^ \(import "[^"]*" "([^"]*)" \(func ' + NAME + r"(.*)$")
TYPE = re.compile(r"^ \(type \$(\S+) \(func(.*)\)\)$")
CALL = re.compile(r"\(call " + NAME)
CALL_INDIRECT = re.compile(r"\(call_indirect \$\S+ \(type \$([^\s)]+)\)")
ELEM_NAME = re.compile(NAME)
PARAM = re.compile(r"\(param((?: \$\S+)? (?:[if](?:32|64)|v128|funcref|externref|exnref)+(?: [if](?:32|64))*)\)")
RESULT = re.compile(r"\(result ((?:[if](?:32|64)|v128)(?: (?:[if](?:32|64)|v128))*)\)")
MAP_SECTION = re.compile(r"^\s+-\s+[0-9a-f]+\s+[0-9a-f]+ (\S+)$")
MAP_INPUT = re.compile(r"^\s+-\s+[0-9a-f]+\s+[0-9a-f]+ {9}(\S.*):\((.*)\)$")


def name_of(m, first=1):
    return m.group(first) if m.group(first) is not None else m.group(first + 1)


def signature(text):
    params = []
    for p in PARAM.findall(text):
        params += [t for t in p.split() if not t.startswith("$")]
    results = []
    for r in RESULT.findall(text):
        results += r.split()
    return " ".join(params) + " -> " + " ".join(results)


def read_map(path):
    """The input object of each function in the CODE section, in function order."""
    objects = []
    section = None
    with open(path) as f:
        for line in f:
            line = line.rstrip("\n")
            m = MAP_SECTION.match(line)
            if m:
                section = m.group(1)
                continue
            if section == "CODE":
                m = MAP_INPUT.match(line)
                if m:
                    objects.append(m.group(1))
    return objects


def read_module(lines):
    mod = {"types": {}, "sig": {}, "calls": defaultdict(set), "indirect": defaultdict(set), "eh": set(),
           "table": set(), "seeds": set(), "order": [], "imports": 0}
    current = None
    for line in lines:
        if line.startswith(" (type "):
            m = TYPE.match(line.rstrip("\n"))
            if m:
                mod["types"][m.group(1)] = signature(m.group(2))
            continue
        if line.startswith(" (import "):
            m = IMPORT.match(line)
            if m:
                name = name_of(m, 2)
                mod["imports"] += 1
                mod["sig"][name] = signature(m.group(4))
                if m.group(1) in ASYNC_IMPORTS or m.group(1).startswith(ASYNC_PREFIXES):
                    mod["seeds"].add(name)
            continue
        if line.startswith(" (elem "):
            for m in ELEM_NAME.finditer(line):
                mod["table"].add(name_of(m))
            continue
        if line.startswith(" (func "):
            m = FUNC.match(line)
            current = name_of(m)
            mod["sig"][current] = signature(m.group(3))
            mod["order"].append(current)
            continue
        if current is None or "call" not in line and not EH.search(line):
            continue
        for m in CALL.finditer(line):
            mod["calls"][current].add(name_of(m))
        for m in CALL_INDIRECT.finditer(line):
            mod["indirect"][current].add(mod["types"][m.group(1)])
        if EH.search(line):
            mod["eh"].add(current)
    mod["table"].discard("0")
    return mod


def unwinding(mod, analyzed):
    """Functions that can unwind, and why (the callee that can); call_indirect counts only in `analyzed`."""
    callers = defaultdict(set)
    for f, cs in mod["calls"].items():
        for c in cs:
            if (f, c) not in NOT_TAKEN:
                callers[c].add(f)
    by_sig = defaultdict(set)
    for f, ss in mod["indirect"].items():
        if analyzed(f):
            for s in ss:
                by_sig[s].add(f)
    why = {s: "async import" for s in mod["seeds"]}
    work = list(mod["seeds"])
    sigs = set()
    while work:
        f = work.pop()
        nxt = [(c, f) for c in callers[f]]
        s = mod["sig"][f]
        if f in mod["table"] and s not in sigs:
            sigs.add(s)
            nxt += [(c, f"call_indirect ({s}): {f}") for c in by_sig[s]]
        for c, reason in nxt:
            if c not in why:
                why[c] = reason
                work.append(c)
    return why, sigs, by_sig


def plain_asyncify(mod):
    """What Asyncify instruments without a list: every function with an indirect call, and their callers."""
    callers = defaultdict(set)
    for f, cs in mod["calls"].items():
        for c in cs:
            callers[c].add(f)
    found = set(mod["seeds"]) | set(mod["indirect"])
    work = list(found)
    while work:
        for c in callers[work.pop()]:
            if c not in found:
                found.add(c)
                work.append(c)
    return found


def check(path, js):
    """After partyboard's link: every entry of the list names a function there that Asyncify instrumented, no
    function calls an async import or an instrumented function directly without being instrumented itself (the
    list was made for another link: WEB_DEBUG's links other variants of Emscripten's ports, which inline otherwise),
    and the JS glue has no async import this didn't start from."""
    with open(js) as f:
        async_js = set(re.findall(r"\b_([A-Za-z0-9_$]+)\.isAsync = true", f.read()))
    unknown = sorted(n for n in async_js if n not in ASYNC_IMPORTS and not n.startswith(ASYNC_PREFIXES))
    if unknown:
        sys.exit(f"web_asyncify_list --check: {js} has async imports {unknown} that tools/web_asyncify_list.py's "
                 f"ASYNC_IMPORTS lacks: add them")
    lines = sys.stdin.readlines()
    mod = read_module(lines)
    # Asyncify's globals have no names in the module. Its data pointer is the one asyncify_start_unwind sets besides
    # the state; every function Asyncify instrumented reads it. (The state is no test: with Asyncify's asserts,
    # PB_WEB_DEBUG, every function reads it.)
    reads, sets = defaultdict(set), defaultdict(set)
    exports = {}
    current = None
    for line in lines:
        if line.startswith(" (func "):
            current = name_of(FUNC.match(line))
        elif line.startswith(' (export "asyncify_'):
            m = re.match(r' \(export "([^"]*)" \(func ' + NAME, line)
            exports[m.group(1)] = name_of(m, 2)
        elif current is not None and "(global." in line:
            for op, g in re.findall(r"\(global\.(get|set) \$([^\s)]+)", line):
                (reads if op == "get" else sets)[current].add(g)
    if "asyncify_get_state" not in exports or "asyncify_start_unwind" not in exports:
        sys.exit("web_asyncify_list --check: no asyncify_get_state/asyncify_start_unwind in the module")
    data = sets[exports["asyncify_start_unwind"]] - reads[exports["asyncify_get_state"]]
    if len(data) != 1:
        sys.exit(f"web_asyncify_list --check: can't tell Asyncify's data global ({sorted(data)})")
    data = next(iter(data))
    runtime = set(exports.values())
    instrumented = {f for f, g in reads.items() if data in g} - runtime
    names = {unescape(f) for f in mod["order"]}
    with open(path) as f:
        entries = [e.rstrip("\n") for e in f if e.strip()]
    exact = [e for e in entries if "*" not in e]
    missing = [e for e in exact if e not in names]
    for e in entries:
        if "*" in e:
            pattern = re.compile(".*".join(map(re.escape, e.split("*"))))
            if not any(pattern.fullmatch(n) for n in names):
                missing.append(e)
    plain_names = {unescape(f) for f in instrumented}
    plain = [e for e in exact if e in names and e not in plain_names]
    unwinds = instrumented | mod["seeds"]
    open_calls = sorted((f, c) for f, cs in mod["calls"].items() if f not in instrumented and f not in mod["eh"]
                        and f not in runtime for c in cs if c in unwinds and (f, c) not in NOT_TAKEN)
    print(f"web_asyncify_list --check: {len(instrumented)} of {len(names)} functions instrumented; "
          f"{len(entries)} entries, {len(missing)} not in the module, {len(plain)} not instrumented; "
          f"{len(open_calls)} direct calls into unwinding code from code that isn't instrumented", file=sys.stderr)
    for e in missing[:10]:
        print(f"  missing: {e}", file=sys.stderr)
    for e in plain[:10]:
        print(f"  not instrumented: {e}", file=sys.stderr)
    for f, c in open_calls[:20]:
        print(f"  not instrumented: {unescape(f)}, which calls {unescape(c)}", file=sys.stderr)
    if missing or plain or open_calls:
        sys.exit(1)


def main():
    if len(sys.argv) == 4 and sys.argv[1] == "--check":
        check(sys.argv[2], sys.argv[3])
        return
    if len(sys.argv) < 3:
        sys.exit("usage: web_asyncify_list.py MAP OUT [REPORT] < probe.wat")
    objects = read_map(sys.argv[1])
    mod = read_module(sys.stdin)
    seeds, eh, order = mod["seeds"], mod["eh"], mod["order"]
    missing = [s for s in ("emscripten_sleep", "emscripten_fiber_swap") if s not in seeds]
    if missing:
        sys.exit(f"web_asyncify_list: no import {missing} in the probe: not the module this expects")
    # Binaryen adds functions after wasm-ld's (STACK_OVERFLOW_CHECK's handler, unnamed): not in the map
    linked = len(order)
    while linked > len(objects) and order[linked - 1].isdigit():
        linked -= 1
    if len(objects) != linked:
        sys.exit(f"web_asyncify_list: {len(order)} functions in the probe, {len(objects)} in its map")
    # a repeated name's `_<index>` must be its place in the map's order (the functions weren't reordered), give or
    # take the same offset for all (the imports wasm-dis counts: one fewer than the probe's with -g2)
    names = set(order)
    numbered = [(i, int(m.group(2))) for i, f in enumerate(order)
                for m in [re.match(r"^(.*)_(\d+)$", f)] if m and m.group(1) in names]
    offsets = defaultdict(int)
    for i, n in numbered:
        offsets[n - i] += 1
    offset = max(offsets, key=offsets.get, default=0)
    moved = [order[i] for i, n in numbered if n != offset + i]
    if len(moved) * 10 > len(numbered):
        sys.exit(f"web_asyncify_list: {len(moved)} of {len(numbered)} repeated names aren't numbered by their place "
                 f"(e.g. {moved[:3]}): the probe's functions moved")
    obj = dict(zip(order, objects))

    everything = os.environ.get("WEB_ASYNCIFY_INDIRECT") == "all"
    analyzed = (lambda f: True) if everything else (lambda f: game_code(obj.get(f, "")) or f in LIBRARY_INDIRECT)
    why, sigs, by_sig = unwinding(mod, analyzed)
    by_signature, _, _ = unwinding(mod, lambda f: True)
    plain = plain_asyncify(mod)
    only = sorted(f for f in why if f not in seeds and f not in eh)

    repeated = {re.sub(r"_\d+$", "", order[i]) for i, _ in numbered}
    entries = []
    for n in only:
        base = re.sub(r"_\d+$", "", n)
        e = unescape(n if n in repeated else base if base in repeated else n)
        if n in repeated or base in repeated:
            e += "*"
        if not balanced(e):
            e = re.sub(r"[()<>\[\]{}]", "*", e)
        entries.append(e)
    entries = list(dict.fromkeys(entries)) + JS_ENTRIES
    with open(sys.argv[2], "w") as f:
        for e in entries:
            f.write(e + "\n")

    by_lib = defaultdict(int)
    for n in only:
        by_lib[re.sub(r"\(.*\)$", "", obj.get(n, "?")).rsplit("/", 1)[-1]] += 1
    def count(found):
        return len([f for f in found if f not in seeds and f not in eh])
    summary = (f"web_asyncify_list: {len(only)} of {len(order)} functions instrumented"
               f"{' (WEB_ASYNCIFY_INDIRECT=all)' if everything else ''}; by signature alone "
               f"{count(by_signature)}, plain Asyncify {count(plain)}; {len(set(why) & eh)} with exception "
               f"handling left out")
    print(summary, file=sys.stderr)
    if len(sys.argv) > 3:
        with open(sys.argv[3], "w") as f:
            f.write(summary + "\n\ninstrumented, by input:\n")
            for lib, n in sorted(by_lib.items(), key=lambda kv: -kv[1]):
                f.write(f"  {n:6} {lib}\n")
            f.write("\nsignatures whose indirect calls can unwind (callers analyzed):\n")
            for s in sorted(sigs):
                f.write(f"  ({s})  {len(by_sig[s])}\n")
            f.write("\nwhy each function is instrumented (a callee that can unwind):\n")
            for n in only:
                f.write(f"{n}\t{why[n]}\t{obj.get(n, '?')}\n")


if __name__ == "__main__":
    main()
