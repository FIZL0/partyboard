#!/usr/bin/env python3
"""Static check of the unsigned IPA from tools/ios-ipa.sh (N86); no device needed.

Usage: tools/ios-ipa-check.py [IPA] [SDK]   (defaults build/partyboard-ios.ipa, ~/.local/share/hst-ios/iPhoneOS.sdk)

For every Mach-O in Payload/PartyBoard.app (the executable and Frameworks/*.dylib):
- arm64, iOS, minos 15.0 (LC_BUILD_VERSION);
- each LC_LOAD_DYLIB resolves: @rpath/@executable_path ones to a file in Frameworks/, system ones to a .tbd in the SDK;
- each import (the chained fixups' table: dylib ordinal + name) is exported by that dylib (our dylibs: llvm-nm -gU;
  system ones: the .tbd and the libraries it re-exports). Flat or dynamic lookups fail; a weak import the SDK lacks
  is listed (it is NULL at run time, as the code that weak-links it expects).
Then: every overlay of include/ovl_table.h has Frameworks/<name>.dylib exporting _ObjectSetup, and Info.plist has the
keys the Files app and the launch need. Prints one line per problem; exit 1 if any.
"""
import os, plistlib, re, struct, subprocess, sys, tempfile, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOKEN = re.compile(r"[A-Za-z0-9_$.]+")
problems = []
weak_missing = set()


def run(*args):
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout


def load_commands(path):
    out = run("llvm-otool", "-l", path)
    cmds, cur = [], None
    for line in out.splitlines():
        line = line.strip()
        if line.startswith("cmd "):
            cur = {"cmd": line.split()[1]}
            cmds.append(cur)
        elif cur is not None and " " in line:
            k, v = line.split(" ", 1)
            cur.setdefault(k, v.strip())
    return cmds


def dylib_name(install):
    """llvm-objdump's dylib column: the leaf without extension or version (libdol, UIKit, libSystem)."""
    leaf = os.path.basename(install)
    return re.sub(r"(\.[0-9A-Z]+)*\.dylib$", "", leaf) if leaf.endswith(".dylib") else leaf


class Sdk:
    def __init__(self, root):
        self.root, self.cache = root, {}

    def tbd(self, install):
        base = os.path.join(self.root, install.lstrip("/"))
        for cand in (re.sub(r"\.dylib$", ".tbd", base), base + ".tbd"):
            if os.path.isfile(cand):
                return cand
        return None

    def symbols(self, install, seen=None):
        if install in self.cache:
            return self.cache[install]
        seen = seen or set()
        seen.add(install)
        path = self.tbd(install)
        syms = set()
        if path:
            text = open(path, encoding="utf-8", errors="replace").read()
            syms = set(TOKEN.findall(text))
            for lib in re.findall(r"'(/[^']+)'", text):  # re-exports kept in their own .tbd (inline ones are in text)
                if lib not in seen:
                    syms |= self.symbols(lib, seen)
        self.cache[install] = syms
        return syms


def objc_names(sym):
    for p in ("_OBJC_CLASS_$_", "_OBJC_METACLASS_$_", "_OBJC_EHTYPE_$_", "_OBJC_IVAR_$_"):
        if sym.startswith(p):
            return [sym, sym[len(p):]]
    return [sym]


def chained_imports(path):
    """(ordinal, weak, name) of each import in LC_DYLD_CHAINED_FIXUPS (lld's default from iOS 15). Not llvm-objdump
    --dyld-info: with DYLD_CHAINED_IMPORT_ADDEND64 (libdol) it names the wrong import for most binds (LLVM 23)."""
    d = open(path, "rb").read()
    magic, _, _, _, ncmds = struct.unpack_from("<5I", d, 0)
    if magic != 0xFEEDFACF:
        problems.append(f"{path}: not a 64-bit Mach-O")
        return []
    off, fixups = 32, None
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", d, off)
        if cmd == 0x80000034:  # LC_DYLD_CHAINED_FIXUPS
            fixups = struct.unpack_from("<I", d, off + 8)[0]
        off += size
    if fixups is None:
        problems.append(f"{path}: no LC_DYLD_CHAINED_FIXUPS (this check only reads those)")
        return []
    _, _, imports_off, symbols_off, count, fmt, _ = struct.unpack_from("<7I", d, fixups)
    out = []
    for i in range(count):
        p = fixups + imports_off + i * {1: 4, 2: 8, 3: 16}[fmt]
        if fmt in (1, 2):  # DYLD_CHAINED_IMPORT(_ADDEND): lib_ordinal:8 weak:1 name_offset:23
            v = struct.unpack_from("<I", d, p)[0]
            ordinal, weak, name_off = v & 0xFF, (v >> 8) & 1, v >> 9
            ordinal = ordinal - 0x100 if ordinal > 0xF0 else ordinal
        else:  # DYLD_CHAINED_IMPORT_ADDEND64: lib_ordinal:16 weak:1 reserved:15 name_offset:32 addend:64
            v = struct.unpack_from("<Q", d, p)[0]
            ordinal, weak, name_off = v & 0xFFFF, (v >> 16) & 1, v >> 32
            ordinal = ordinal - 0x10000 if ordinal > 0xFFF0 else ordinal
        name = d[fixups + symbols_off + name_off:].split(b"\0", 1)[0].decode()
        out.append((ordinal, weak, name))
    return out


def check_macho(path, app, sdk, exports, weak_definers):
    rel = os.path.relpath(path, app)
    cmds = load_commands(path)
    bv = [c for c in cmds if c["cmd"] == "LC_BUILD_VERSION"]
    if not bv or bv[0].get("platform") not in ("2", "IOS") or bv[0].get("minos") != "15.0":
        problems.append(f"{rel}: build version {bv[0] if bv else None}, want iOS minos 15.0")
    if "arm64" not in run("llvm-objdump", "--macho", "--private-header", path).lower():
        problems.append(f"{rel}: not arm64")
    rpaths = [c["path"].split(" (offset")[0] for c in cmds if c["cmd"] == "LC_RPATH"]
    for rp in rpaths:
        if not rp.startswith(("@executable_path", "@loader_path")):
            problems.append(f"{rel}: rpath {rp} points outside the app")
    deps = []  # in load command order: a two-level import's ordinal n is deps[n - 1]
    for c in cmds:
        if c["cmd"] in ("LC_LOAD_DYLIB", "LC_LOAD_WEAK_DYLIB", "LC_REEXPORT_DYLIB", "LC_LAZY_LOAD_DYLIB",
                        "LC_LOAD_UPWARD_DYLIB"):
            name = c["name"].split(" (offset")[0]
            if name.startswith(("@rpath/", "@executable_path/Frameworks/")):
                leaf = name.split("/")[-1]
                if not os.path.isfile(os.path.join(app, "Frameworks", leaf)):
                    problems.append(f"{rel}: needs {name}, not in Frameworks/")
                deps.append(("ours", leaf))
            elif name.startswith("@"):
                problems.append(f"{rel}: load path {name}")
                deps.append(("bad", name))
            else:
                if not sdk.tbd(name):
                    problems.append(f"{rel}: needs {name}, not in the SDK")
                deps.append(("sdk", name))
    imports = chained_imports(path)
    for ordinal, weak, sym in imports:
        if ordinal in (0, -1):  # this image, the main executable
            continue
        if ordinal == -3:  # dyld's weak-definition lookup over the loaded images (OSReport: DECL_WEAK in aurora)
            if not any(sym in exports[k] for k in weak_definers) and \
                    not any(sym in sdk.symbols(w) for k, w in deps if k == "sdk"):  # libc++'s operator new etc.
                problems.append(f"{rel}: weak {sym} defined by no image with WEAK_DEFINES nor a system library")
            continue
        if ordinal < 0 or ordinal > len(deps):
            problems.append(f"{rel}: {sym} bound by ordinal {ordinal} (flat or dynamic lookup)")
            continue
        kind, where = deps[ordinal - 1]
        have = exports.get(where, set()) if kind == "ours" else sdk.symbols(where)
        if not any(n in have for n in objc_names(sym)):
            if weak and kind == "sdk":
                weak_missing.add(f"{where.split('/')[-1]}:{sym}")  # weak import of an API the SDK lacks: NULL
            else:
                problems.append(f"{rel}: {sym} not exported by {where}")
    return len(imports), sorted(set(os.path.basename(w) for _, w in deps))


def main():
    ipa = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build/partyboard-ios.ipa")
    sdk = Sdk(sys.argv[2] if len(sys.argv) > 2 else os.path.expanduser("~/.local/share/hst-ios/iPhoneOS.sdk"))
    with tempfile.TemporaryDirectory() as tmp:
        zipfile.ZipFile(ipa).extractall(tmp)
        app = os.path.join(tmp, "Payload", "PartyBoard.app")
        fw = os.path.join(app, "Frameworks")
        libs = sorted(os.path.join(fw, f) for f in os.listdir(fw))
        exports = {os.path.basename(l): set(s.split()[-1] for s in run("llvm-nm", "-gU", l).splitlines() if s.strip())
                   for l in libs}
        weak_definers = [os.path.basename(l) for l in libs if "WEAK_DEFINES" in run("llvm-otool", "-hv", l)]
        exe = os.path.join(app, "PartyBoard")
        total = 0
        for m in [exe] + libs:
            n, deps = check_macho(m, app, sdk, exports, weak_definers)
            total += n
            if m in (exe, os.path.join(fw, "libdol.dylib"), os.path.join(fw, "w01Dll.dylib")):
                print(f"{os.path.relpath(m, app)}: {n} imports from {', '.join(deps)}")
        # The PC table as src/game/ovllist.c sees it (the file also holds the GC names, under #else).
        table = run("clang", "-E", "-P", "-DTARGET_PC", "-DVERSION=1", "-DDLL(n)=OVL:n", "-I", os.path.join(ROOT, "include"),
                    os.path.join(ROOT, "include/ovl_table.h"))
        ovls = re.findall(r"OVL:(\w+)", table)
        no_source, rel_files = [], open(os.path.join(ROOT, "files.cmake")).read()
        for o in ovls:
            leaf = f"{o}.dylib"
            if f"src/REL/{o}/" not in rel_files:
                no_source.append(o)  # not in files.cmake's REL_FILES: no platform builds it
            elif leaf not in exports:
                problems.append(f"overlay {o}: no Frameworks/{leaf}")
            elif "_ObjectSetup" not in exports[leaf]:
                problems.append(f"overlay {o}: {leaf} does not export _ObjectSetup")
        plist = plistlib.load(open(os.path.join(app, "Info.plist"), "rb"))
        want = {"CFBundleExecutable": "PartyBoard", "UIFileSharingEnabled": True, "LSSupportsOpeningDocumentsInPlace": True,
                "MinimumOSVersion": "15.0", "CFBundlePackageType": "APPL"}
        for k, v in want.items():
            if plist.get(k) != v:
                problems.append(f"Info.plist: {k} = {plist.get(k)!r}, want {v!r}")
        for f in ("Assets.car", "AppIcon60x60@2x.png", "Base.lproj/LaunchScreen.storyboardc/Info.plist", "res/rml"):
            if not os.path.exists(os.path.join(app, f)):
                problems.append(f"app: no {f}")
        print(f"{1 + len(libs)} Mach-O files, {total} imports, {len(ovls)} overlay entries "
              f"({len(set(ovls) - set(no_source))} built, not in REL_FILES: {' '.join(sorted(set(no_source)))})")
        if weak_missing:
            print(f"weak imports the SDK lacks ({len(weak_missing)}): {' '.join(sorted(weak_missing))}")
    for p in problems:
        print("PROBLEM", p)
    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()
