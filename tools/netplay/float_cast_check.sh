#!/usr/bin/env bash
# N91: lists the game's float -> integer conversions whose value doesn't fit the integer type. In C that is undefined,
# and the platforms disagree: x86_64 (GCC) wraps through a 32/64-bit convert (-1.7f -> u8 255), wasm32 (clang) clamps
# unsigned conversions (-1.7f -> u8 0), so a browser peer and a desktop peer leave lockstep there (m415, N91). Fix a
# site by converting through s32 first: `(u8)(s32)f` is defined and wraps like x86 and the GameCube's fctiwz + stb.
#
# Builds build/n106-fco (the desktop preset with GCC's -fsanitize=float-cast-overflow on all C: game, RELs, aurora,
# and -DPB_FCO_BOOT for bootDll's jump below), runs SCRIPT with --match 0@1500 --turbo in a fresh user dir and prints
# each reported source line once, with the first value. Needs the disc (PB_DISC) and a desktop session for the hidden
# window, like party_check.sh.
#
#   tools/netplay/float_cast_check.sh [SCRIPT] [out dir]   (default tools/netplay/party.script, build/fco-check)
#
# Environment (N106):
#   FCO_MATCH=<board>@<frame>  the --match (default 0@1500; 1..5 are w02..w06)
#   FCO_OVL=<overlay index>    no --match: boot straight into that minigame or board (w01..w21, w10 = the tutorial)
#                              through bootDll's PB_FCO_OVL jump, 4 COMs; SCRIPT then wants only `end <frame>` and
#                              pads. Indices: `cpp -P -DTARGET_PC -Iinclude include/ovl_table.h`, from 0.
#   FCO_HUMAN=<mask>           with FCO_OVL: these seats are human (pad = seat), playing SCRIPT's pads
#   FCO_NOBUILD=1              skip the build (parallel runs of one build)
#   FCO_TIMEOUT=<s>            the run's time limit (default 1500)
#   UBSAN_OPTIONS=print_stacktrace=1   each report's callers (an inline helper's site says little alone)
# Every board and minigame at once: research/journal/2026-10-28-n106-float-casts/sweep.sh.
#
# Exit status: 0 no reports, 1 reports (or the run failed).
set -euo pipefail
cd "$(dirname "$0")/../.."
root=$PWD
script=$(realpath "${1:-tools/netplay/party.script}")
out=${2:-build/fco-check}
limit=${FCO_TIMEOUT:-1500}
bdir=build/n106-fco
flag=-fsanitize=float-cast-overflow
if [ ! -f "$bdir/build.ninja" ]; then
    cmake --preset linux-default-relwithdebinfo -B "$bdir" -DCMAKE_C_FLAGS="$flag -DPB_FCO_BOOT" \
        -DCMAKE_SHARED_LINKER_FLAGS="$flag" -DCMAKE_EXE_LINKER_FLAGS="$flag" >/dev/null 2>&1
fi
if [ -z "${FCO_NOBUILD:-}" ]; then
    cmake --build "$bdir" >"$bdir/fco-build.log" 2>&1 || { tail -20 "$bdir/fco-build.log"; exit 1; }
fi
rm -rf "$out"
mkdir -p "$out/xdg"
out=$(cd "$out" && pwd)
if [ -n "${FCO_OVL:-}" ]; then
    mkdir -p "$out/xdg/MarioPartyRD/Party Board"
    printf '{\n    "backend.skipBootSequence": true,\n    "backend.skipPreLaunchUI": true\n}\n' \
        >"$out/xdg/MarioPartyRD/Party Board/config.json"
    run=(env PB_FCO_OVL="$FCO_OVL" PB_FCO_HUMAN="${FCO_HUMAN:-0}")
    set --
else
    run=(env)
    set -- --match "${FCO_MATCH:-0@1500}"
fi
# The game ignores SIGTERM: KILL at the limit.
"${run[@]}" UBSAN_OPTIONS=${UBSAN_OPTIONS:-print_stacktrace=0} XDG_DATA_HOME="$out/xdg" HOME="$out" timeout -s KILL "$limit" \
    "$root/$bdir/partyboard" --script "$script" "$@" --turbo --hash-log "$out/hash.log" >"$out/out.txt" 2>&1 || true
frames=$(wc -l <"$out/hash.log" 2>/dev/null || echo 0)
echo "$frames frames, log in $out/out.txt"
if grep -a "runtime error: .* is outside the range" "$out/out.txt" | sed "s|^$root/||" | sort -u -t: -k1,2; then
    exit 1
fi
if [ "$frames" -eq 0 ]; then
    echo "the run logged no frames"
    exit 1
fi
echo "no out-of-range float conversions"
