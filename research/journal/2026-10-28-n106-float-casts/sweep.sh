#!/usr/bin/env bash
# N106: sweep.sh <out dir> [jobs] [list]: float_cast_check.sh over every minigame (all COM, then all four seats human on
# wander.script's random pads), every board w01-w21 all COM (boot jump, 10 turns) and --match parties on w01-w06
# (party.script, seat 0 human on A taps). Needs build/n106-fco built (float_cast_check.sh once). One line per run in
# <out>/summary.txt; reported sites, each once, in <out>/sites.txt.
#   list lines: <name> <env...>   (default: all of the above)
set -uo pipefail
cd "$(dirname "$0")/../../.."
J=research/journal/2026-10-28-n106-float-casts
out=$(realpath -m "$1"); jobs=${2:-4}
mkdir -p "$out"
list=$out/list.txt
if [ -n "${3:-}" ]; then cp "$3" "$list"; else
    : >"$list"
    # The long runs first. Overlays 9..69 = m401..m463 (4..8, m300..m333, are not built on PC).
    for o in $(seq 89 97); do echo "bd$o-com FCO_OVL=$o FCO_TIMEOUT=3000 S=$J/board.script" >>"$list"; done
    for b in 0 1 2 3 4 5; do echo "match$b FCO_MATCH=$b@1500 FCO_TIMEOUT=3000 S=tools/netplay/party.script" >>"$list"; done
    for o in $(seq 9 69); do echo "mg$o-com FCO_OVL=$o FCO_TIMEOUT=600 S=$J/com.script" >>"$list"; done
    for o in $(seq 9 69); do echo "mg$o-hum FCO_OVL=$o FCO_HUMAN=15 FCO_TIMEOUT=600 S=$J/wander.script" >>"$list"; done
fi
one() {
    name=$1; shift
    env "$@" FCO_NOBUILD=1 bash -c 'tools/netplay/float_cast_check.sh "$S" "$0"' "$out/$name" >"$out/$name.res" 2>&1
    echo "$name $? $(grep -a ' frames, log' "$out/$name.res" | cut -d' ' -f1) frames $(grep -ac 'runtime error' "$out/$name.res") sites"
}
export -f one; export out
xargs -P "$jobs" -L 1 bash -c 'one "$@"' _ <"$list" | tee "$out/summary.txt"
cat "$out"/*.res | grep -a "runtime error" | sort -u -t: -k1,2 >"$out/sites.txt"
wc -l "$out/sites.txt"
