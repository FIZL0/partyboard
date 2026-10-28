#!/bin/bash
# run.sh <name> <secs> -- partyboard args...   (cwd: anywhere; N132's run.sh with its runs under build/n129/)
# One game in its own user dir build/n129/<name> (vsync off, skipBootSequence off). Extra env (PB_ICON_SHOT=...)
# passes through. The game ignores SIGTERM: KILL at <secs>.
name=$1; secs=$2; shift 2
W=$(cd "$(dirname "$0")/../../../.." && pwd)
U=$W/build/n129/$name
rm -rf "$U"
[ "$1" = -- ] && shift
mkdir -p "$U/MarioPartyRD/Party Board"
echo '{"video.enableVsync": false, "backend.skipBootSequence": false}' > "$U/MarioPartyRD/Party Board/config.json"
cd "$U"
WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-1} XDG_DATA_HOME=$U HOME=$U timeout -s KILL "$secs" \
    "$W/build/linux-default-relwithdebinfo/partyboard" "$@" > "$U/out.log" 2>&1
echo "exit $?"
