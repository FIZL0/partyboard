#!/bin/bash
# run.sh <userdir> <secs> -- partyboard args...   One game in its own user dir (vsync off, skipBootSequence off; an
# existing dir keeps its card and autosave). Extra env (PB_RELAUNCH_TEST=..., PB_CONTINUE=...) passes through.
U=$1; secs=$2; shift 2
W=$(cd "$(dirname "$0")/../../../.." && pwd)
[ "$1" = -- ] && shift
mkdir -p "$U/MarioPartyRD/Party Board"
cfg="$U/MarioPartyRD/Party Board/config.json"
[ -f "$cfg" ] || echo '{"video.enableVsync": false, "backend.skipBootSequence": false}' > "$cfg"
cd "$U"
WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-1} XDG_DATA_HOME=$U HOME=$U timeout -s KILL "$secs" "$W/build/linux-default-relwithdebinfo/partyboard" "$@" > "$U/out.log" 2>&1
echo "exit $?"
