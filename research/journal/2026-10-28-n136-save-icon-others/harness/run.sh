#!/bin/bash
# run.sh <name> <secs> -- partyboard args...   (N129's run.sh, runs under .n136-runs/<name>)
# One game in its own user dir (no config: defaults, as party_check.sh). Extra env (PB_ICON_SHOT=...) passes
# through. GDB=<file>: run it under gdb -batch -x <file>. The game ignores SIGTERM: KILL at <secs>.
name=$1; secs=$2; shift 2
W=$(cd "$(dirname "$0")/../../../.." && pwd)
U=$W/.n136-runs/$name
rm -rf "$U"
[ "$1" = -- ] && shift
mkdir -p "$U/xdg"
# CONFIG=<json>: the run's config.json (e.g. skipBootSequence for wander-mgmode.script).
if [ -n "$CONFIG" ]; then
    mkdir -p "$U/xdg/MarioPartyRD/Party Board"
    echo "$CONFIG" > "$U/xdg/MarioPartyRD/Party Board/config.json"
fi
cd "$U"
WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-1} XDG_DATA_HOME=$U/xdg HOME=$U timeout -s KILL "$secs" \
    ${GDB:+gdb -batch -x "$GDB" --args} "$W/build/linux-default-relwithdebinfo/partyboard" "$@" > "$U/out.log" 2>&1
echo "exit $?"
