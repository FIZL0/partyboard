#!/usr/bin/env bash
# N91: lists the game's float -> integer conversions whose value doesn't fit the integer type. In C that is undefined,
# and the platforms disagree: x86_64 (GCC) wraps through a 32/64-bit convert (-1.7f -> u8 255), wasm32 (clang) clamps
# unsigned conversions (-1.7f -> u8 0), so a browser peer and a desktop peer leave lockstep there (m415, N91). Fix a
# site by converting through s32 first: `(u8)(s32)f` is defined and wraps like x86 and the GameCube's fctiwz + stb.
#
# Builds build/n91-fco (the desktop preset with GCC's -fsanitize=float-cast-overflow on all C: game, RELs, aurora),
# runs SCRIPT with --match 0@1500 --turbo in a fresh user dir and prints each reported source line once, with the
# first value. Needs the disc (PB_DISC) and a desktop session for the hidden window, like party_check.sh.
#
#   tools/netplay/float_cast_check.sh [SCRIPT] [out dir]   (default tools/netplay/party.script, build/fco-check)
#
# Exit status: 0 no reports, 1 reports (or the run failed).
set -euo pipefail
cd "$(dirname "$0")/../.."
root=$PWD
script=$(realpath "${1:-tools/netplay/party.script}")
out=${2:-build/fco-check}
limit=${FCO_TIMEOUT:-1500}
bdir=build/n91-fco
flag=-fsanitize=float-cast-overflow
if [ ! -f "$bdir/build.ninja" ]; then
    cmake --preset linux-default-relwithdebinfo -B "$bdir" -DCMAKE_C_FLAGS="$flag" \
        -DCMAKE_SHARED_LINKER_FLAGS="$flag" -DCMAKE_EXE_LINKER_FLAGS="$flag" >/dev/null 2>&1
fi
cmake --build "$bdir" >"$bdir/fco-build.log" 2>&1 || { tail -20 "$bdir/fco-build.log"; exit 1; }
rm -rf "$out"
mkdir -p "$out/xdg"
out=$(cd "$out" && pwd)
# The game ignores SIGTERM: KILL at the limit.
UBSAN_OPTIONS=print_stacktrace=0 XDG_DATA_HOME=$out/xdg HOME=$out timeout -s KILL "$limit" "$root/$bdir/partyboard" \
    --script "$script" --match 0@1500 --turbo --hash-log "$out/hash.log" >"$out/out.txt" 2>&1 || true
echo "$(wc -l <"$out/hash.log") frames, log in $out/out.txt"
if grep -a "runtime error: .* is outside the range" "$out/out.txt" | sed "s|^$root/||" | sort -u -t: -k1,2; then
    exit 1
fi
echo "no out-of-range float conversions"
