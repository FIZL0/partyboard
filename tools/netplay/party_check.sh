#!/usr/bin/env bash
# N53: two runs of one build with the same pads must play the same party. Runs tools/netplay/party.script (a whole
# --match party on w01: 10 turns, the minigames, the results, `match over`) twice in parallel, each in a fresh user
# dir, and compares the hash logs with hashdiff.py. Needs the disc (PB_DISC or the saved setting) and a desktop
# session for the hidden game windows; takes a few minutes (--turbo). Not part of tools/check.sh for those reasons.
#
#   tools/netplay/party_check.sh [out dir]      (default build/party-check; logs stay there)
#   tools/netplay/gpu_busy.sh tools/netplay/party_check.sh   (N120: the GPU's busy percent over the runs)
#
# Exit status: 0 identical and both reached `match over`, 1 otherwise.
set -euo pipefail
cd "$(dirname "$0")/../.."
root=$PWD
game=$root/build/linux-default-relwithdebinfo/partyboard
out=${1:-build/party-check}
mkdir -p "$out"
out=$(cd "$out" && pwd)
limit=${PARTY_CHECK_TIMEOUT:-1500}

run() {
    local d=$out/run$1
    rm -rf "$d"
    mkdir -p "$d/xdg"
    # The game ignores SIGTERM: KILL at the limit.
    XDG_DATA_HOME=$d/xdg HOME=$d timeout -s KILL "$limit" "$game" --script "$root/tools/netplay/party.script" \
        --match 0@1500 --turbo --hash-log "$d/hash.log" >"$d/out.txt" 2>&1 || true
}

run 1 &
run 2 &
wait
status=0
for i in 1 2; do
    over=$(grep -a -m1 "netplay: match over at frame" "$out/run$i/out.txt" || true)
    echo "run$i: $(wc -l <"$out/run$i/hash.log") frames, ${over:-no match over}"
    [ -n "$over" ] || status=1
done
tools/netplay/hashdiff.py "$out/run1/hash.log" "$out/run2/hash.log" || status=1
exit $status
