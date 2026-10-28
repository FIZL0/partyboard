#!/bin/bash
# run.sh <name> <secs> [cfg-json] -- partyboard args...   (needs harness.diff applied + a build)
# One game in its own user dir under <worktree>/.n130-runs/<name>; shots go to that dir (PB_SHOT_AT frames).
# Extra env (PB_SAFE, PB_SHOT_AT) passes through.
name=$1; secs=$2; shift 2
cfgjson='{"video.enableVsync": false}'
if [ "$1" != -- ] && [ -n "$1" ]; then cfgjson=$1; shift; fi
[ "$1" = -- ] && shift
W=$(cd "$(dirname "$0")/../../../.." && pwd)
U=$W/.n130-runs/$name
rm -rf "$U"; mkdir -p "$U/MarioPartyRD/Party Board"
echo "$cfgjson" > "$U/MarioPartyRD/Party Board/config.json"
cd "$U"
PB_WINDOW_SHOT=1 PB_SHOT_DIR=$U WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-1} XDG_DATA_HOME=$U HOME=$U \
  timeout -s KILL "$secs" "$W/build/linux-default-relwithdebinfo/partyboard" "$@" > "$U/out.log" 2>&1
echo "exit $?"
