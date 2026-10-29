#!/bin/sh
# N148: run_web.sh <name> [SECS]: as N96's run.sh: this worktree's desktop build (apply.py's harness, PB_N148_RTC)
# hosts for browsers and opens the lobby window; one headless Chromium tab of build/web (n148_pair.mjs) joins, never
# readies, and the desktop presses Start Match and the confirm's Start. Both mash A for SECS (default 200).
# Out: .n148-runs/<name>/.
cd "$(dirname "$0")/../../../.."
R=$PWD/.n148-runs/$1
SECS=${2:-200}
rm -rf "$R"
mkdir -p "$R/x" "$R/desk/data/MarioPartyRD/Party Board"
printf '{"video.enableVsync": false, "backend.skipPreLaunchUI": true}\n' > "$R/desk/data/MarioPartyRD/Party Board/config.json"
cd build/linux-default-relwithdebinfo
env WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-1} XDG_DATA_HOME="$R/desk/data" HOME="$R/desk" PB_N148="$R/x" PB_N148_RTC=1 \
  timeout -s KILL $((SECS + 400)) ./partyboard --hash-log "$R/desk.hash" > "$R/desk.out" 2>&1 < /dev/null &
DESK=$!
cd ../..
WEB_ROOT="$PWD/build/web" PAIR_DESKTOP="$R/x" PAIR_ARGS="--hash-log /tmp/h.log" DUMP_FILES=/tmp/h.log WEB_PORT=${WEB_PORT:-8796} \
  timeout -s KILL $((SECS + 380)) node research/journal/2026-10-29-n148-start-unready/harness/n148_pair.mjs \
  "$PB_DISC" "$R/web" "$SECS" || true
kill -9 $DESK 2>/dev/null || true
wait $DESK 2>/dev/null || true
grep -E "n148:|netplay: (desync|lobby started|match over)" "$R/desk.out" | cut -c1-400 | head -40
tail -5 "$R/web/pair.txt"
