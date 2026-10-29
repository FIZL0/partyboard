#!/bin/sh
# N96: run.sh <name> [SECS]: a desktop game (this worktree's build with apply.py's harness) hosts a lobby for browsers;
# one headless Chromium tab of build/web (n96_pair.mjs) joins it with the code, the answer goes back, the desktop
# starts the match with CPUs in seats 2 and 3, and both mash A for SECS (default 150). Out: build/n96run/<name>/.
# N139: copy of N96's; N139_RELAY (default n0: short codes; off: offer/answer codes) for both sides.
# GUEST=1: the other way round (the tab hosts, the desktop joins with its code and answers).
# N139a: copy of N139's; the tab finds the desktop's lobby in its LAN list (PAIR_NEARBY=1) and joins it from there.
set -e
cd "$(dirname "$0")/../../../.."
R=$PWD/.n139a-runs/desk-$1
SECS=${2:-150}
rm -rf "$R"
mkdir -p "$R/x" "$R/desk/data/MarioPartyRD/Party Board"
printf '{"video.enableVsync": false, "backend.skipPreLaunchUI": true}\n' > "$R/desk/data/MarioPartyRD/Party Board/config.json"
cd build/linux-default-relwithdebinfo
env XDG_DATA_HOME="$R/desk/data" HOME="$R/desk" PB_N96="$R/x" PB_N139_RELAY=${N139_RELAY:-n0} ${GUEST:+PB_N96_GUEST=1} timeout -s KILL $((SECS + 400)) \
  ./partyboard --hash-log "$R/desk.hash" > "$R/desk.out" 2>&1 < /dev/null &
DESK=$!
cd ../..
PAIR_NEARBY=${PAIR_NEARBY-1} N139_RELAY=${N139_RELAY:-n0} WEB_ROOT="$PWD/build/web" PAIR_DESKTOP="$R/x" PAIR_DESKTOP_GUEST=$GUEST PAIR_ARGS="--hash-log /tmp/h.log" DUMP_FILES=/tmp/h.log WEB_PORT=${WEB_PORT:-8796} \
  timeout -s KILL $((SECS + 380)) node research/journal/2026-10-28-n139a-nearby-lobbies/harness/n96_pair.mjs \
  "$PB_DISC" "$R/web" "$SECS" || true
kill -9 $DESK 2>/dev/null || true
wait $DESK 2>/dev/null || true
grep -E "n96:|netplay: (desync|lobby started|match over)|pbnet" "$R/desk.out" | head -40
tail -5 "$R/web/pair.txt"
