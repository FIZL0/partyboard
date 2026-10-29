#!/bin/bash
# run.sh <name> <secs> [cfg-json] -- partyboard args...   (needs harness.diff applied + a build)
# One game in its own user dir under <worktree>/.n147-runs/<name>; shots and out.log go there.
# Env passes through: PB_PAD, PB_MOUSE, PB_TRACE, PB_SHOTS, PB_WIN, PB_FAKE_CONTINUE, PB_SAFE, PB_DISC.
# SIZE=WxH: Hyprland resizes the game's window (by its pid) 3 s after start, as N141's run.sh (scale 1.6 = dp ratio).
name=$1; secs=$2; shift 2
cfgjson='{"video.enableVsync": false}'
if [ "$1" != -- ] && [ -n "$1" ]; then cfgjson=$1; shift; fi
[ "$1" = -- ] && shift
W=$(cd "$(dirname "$0")/../../../.." && pwd)
U=$W/.n147-runs/$name
rm -rf "$U"; mkdir -p "$U/MarioPartyRD/Party Board"
echo "$cfgjson" > "$U/MarioPartyRD/Party Board/config.json"
cd "$U"
PB_HOME_MUSIC=0 PB_SHOT_DIR=$U WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-1} XDG_DATA_HOME=$U HOME=$U \
  timeout -s KILL "$secs" "$W/build/linux-default-relwithdebinfo/partyboard" "$@" > "$U/out.log" 2>&1 &
tpid=$!
if [ -n "$SIZE" ]; then
  sleep 3
  gpid=$(pgrep -P "$tpid" | head -1)
  hyprctl eval "hl.dispatch(hl.dsp.window.resize({ x = ${SIZE%x*}, y = ${SIZE#*x}, relative = false, window = 'pid:$gpid' }))" > /dev/null
fi
wait $tpid
echo "exit $?"
