#!/usr/bin/env bash
# Checks a built tree for the netplay time hooks (cmake/netplay.cmake, both routes): every REL must import
# __wrap_OSGetTime/__wrap_OSGetTick, never the bare OSGetTime/OSGetTick, and dol must export the wrappers.
# Works on ELF (.so), Mach-O (.dylib) and PE (.dll) with llvm-nm/llvm-objdump, so the same command checks
# a Linux, macOS or Windows (MSVC or MinGW) build:
#   tools/netplay/check-hook-imports.sh build/macos-default-relwithdebinfo
# Exit 0 when the hooks are in place, 1 with the offending RELs listed otherwise.
set -euo pipefail
dir=${1:?usage: check-hook-imports.sh <build dir>}
nm=${LLVM_NM:-$(command -v llvm-nm || command -v nm)}
objdump=${LLVM_OBJDUMP:-$(command -v llvm-objdump || command -v objdump)}

# Undefined (imported) symbols of one shared library, without Mach-O's leading underscore.
imports() {
    case $1 in
        *.dll) "$objdump" -p "$1" | awk '/Hint\/Ord/ {p=1; next} /^[[:space:]]*$/ {p=0} p && $1 ~ /^[0-9]+$/ {print $2}' ;;
        *) "$nm" -u -D "$1" 2>/dev/null || "$nm" -u "$1" ;;
    esac | awk '{print $NF}' | demangle "$1"
}

# Mach-O prefixes C symbols with an underscore; ELF symbol versions (@...) are dropped.
demangle() {
    case $1 in
        *.dylib) sed 's/@.*//; s/^_//' ;;
        *) sed 's/@.*//' ;;
    esac | sort -u
}

# Exported symbols of dol.
exports() {
    case $1 in
        *.dll) "$objdump" -p "$1" | awk '/^Export Table:/ {p=1} p && /^[[:space:]]*$/ {p=0} p && $1 ~ /^[0-9]+$/ && $2 ~ /^0x/ {print $3}' ;;
        *) "$nm" -g --defined-only -D "$1" 2>/dev/null || "$nm" -g --defined-only "$1" ;;
    esac | awk '{print $NF}' | demangle "$1"
}

dol=$(find "$dir" -maxdepth 3 \( -name libdol.so -o -name libdol.dylib -o -name dol.dll -o -name libdol.dll \) | head -1)
[ -n "$dol" ] || { echo "no dol library under $dir" >&2; exit 1; }
mapfile -t rels < <(find "$dir" -maxdepth 3 \( -name '*[Dd]ll.so' -o -name '*[Dd]ll.dylib' -o -name '*[Dd]ll.dll' \) | sort)
[ ${#rels[@]} -gt 0 ] || { echo "no REL libraries under $dir" >&2; exit 1; }

fail=0
dolexp=$(exports "$dol")
for s in __wrap_OSGetTime __wrap_OSGetTick __wrap_PADRead; do
    if ! grep -qx "$s" <<<"$dolexp"; then echo "FAIL $dol: does not export $s"; fail=1; fi
done
wrapped=0
for rel in "${rels[@]}"; do
    imp=$(imports "$rel")
    bare=$(grep -xE 'OSGetTime|OSGetTick' <<<"$imp" | tr '\n' ' ' || true)
    if [ -n "$bare" ]; then echo "FAIL $rel: imports $bare"; fail=1; fi
    w=$(grep -xE '__wrap_OSGetTime|__wrap_OSGetTick' <<<"$imp" | tr '\n' ' ' || true)
    [ -n "$w" ] && { wrapped=$((wrapped + 1)); [ -n "${VERBOSE:-}" ] && echo "ok   $rel: $w"; }
done
# Some RELs never ask for the time; at least the boot and mode-select ones do.
if [ "$wrapped" -eq 0 ]; then echo "FAIL no REL imports __wrap_OSGetTime/__wrap_OSGetTick"; fail=1; fi
echo "$dol, ${#rels[@]} RELs, $wrapped import the wrappers: $([ $fail = 0 ] && echo ok || echo FAILED)"
exit $fail
