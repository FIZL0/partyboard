#!/usr/bin/env bash
# N131: tools/netplay/party.script (--match 0@1500 --turbo) as is and with `fast-menus on`, in parallel, then hashdiff.
set -uo pipefail
cd "$(dirname "$0")/../../../.."
out=build/n131/party; mkdir -p $out
{ echo "fast-menus on"; cat tools/netplay/party.script; } > $out/party-fast.script
run() {
    local d=$PWD/$out/$1; rm -rf "$d"; mkdir -p "$d/xdg"
    WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-1} XDG_DATA_HOME=$d/xdg HOME=$d timeout -s KILL 1500 \
        build/linux-default-relwithdebinfo/partyboard --script "$2" --match 0@1500 --turbo --hash-log "$d/hash.log" >"$d/out.txt" 2>&1
}
run base tools/netplay/party.script & a=$!
run fast $out/party-fast.script & b=$!
wait $a; wait $b
for r in base fast; do echo "$r: $(wc -l <$out/$r/hash.log) frames, $(grep -a -m1 'netplay: match over at frame' $out/$r/out.txt)"; done
tools/netplay/hashdiff.py $out/base/hash.log $out/fast/hash.log
# the menus before the match differ by design: compare from the match start (--match 0@1500)
for r in base fast; do awk '$1>=1500' $out/$r/hash.log > $out/$r/match.log; done
tools/netplay/hashdiff.py $out/base/match.log $out/fast/match.log
