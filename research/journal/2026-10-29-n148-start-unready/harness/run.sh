#!/bin/sh
# N148: run.sh <name> [SECS]: two copies of this worktree's build (apply.py's harness), each in its own user dir.
# Copy 1 hosts (iroh, relay off) and opens the lobby window; copy 2 joins by the long code, picks Wario, never
# readies and opens Leave Lobby's confirm; then the host presses Start Match and the modal's Start. Both mash A for
# SECS (default 200) with --hash-log. Out: .n148-runs/<name>/.
cd "$(dirname "$0")/../../../.."
R=$PWD/.n148-runs/$1
SECS=${2:-200}
rm -rf "$R"
mkdir -p "$R/x"
for p in host guest; do
  mkdir -p "$R/$p/data/MarioPartyRD/Party Board"
  printf '{"video.enableVsync": false, "backend.skipPreLaunchUI": true}\n' > "$R/$p/data/MarioPartyRD/Party Board/config.json"
done
cd build/linux-default-relwithdebinfo
export WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-1}
env XDG_DATA_HOME="$R/host/data" HOME="$R/host" PB_N148="$R/x" timeout -s KILL $((SECS + 60)) \
  ./partyboard --hash-log "$R/host.hash" > "$R/host.out" 2>&1 < /dev/null &
HOST=$!
env XDG_DATA_HOME="$R/guest/data" HOME="$R/guest" PB_N148="$R/x" PB_N148_GUEST=1 timeout -s KILL $((SECS + 60)) \
  ./partyboard --hash-log "$R/guest.hash" > "$R/guest.out" 2>&1 < /dev/null &
GUEST=$!
for i in $(seq 1 120); do [ -e "$R/x/guestwait.txt" ] && break; sleep 1; done
sleep 1
echo 1 > "$R/x/start.txt"
sleep "$SECS"
kill -9 $HOST $GUEST 2>/dev/null
wait $HOST $GUEST 2>/dev/null
grep -h "n148:\|netplay: \(desync\|lobby started\)" "$R/host.out" | sed 's/^/host  /' | cut -c1-700
grep -h "n148:\|netplay: \(desync\|lobby started\)" "$R/guest.out" | sed 's/^/guest /' | cut -c1-700
