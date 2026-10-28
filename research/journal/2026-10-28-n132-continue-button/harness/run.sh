#!/bin/bash
# run.sh <name> <secs> [from <name>] -- partyboard args...   (cwd: anywhere)
# One game in its own user dir under <worktree>/.n132-runs/<name> (vsync off, skipBootSequence off). "from <other>"
# starts with a copy of another run's user dir (its card and party autosave). Extra env (PB_CONTINUE=...) passes through.
name=$1; secs=$2; shift 2
W=$(cd "$(dirname "$0")/../../../.." && pwd)
R=$W/.n132-runs
U=$R/$name
rm -rf "$U"
if [ "$1" = from ]; then cp -a "$R/$2" "$U"; rm -f "$U/out.log"; shift 2; fi
[ "$1" = -- ] && shift
mkdir -p "$U/MarioPartyRD/Party Board"
cfg="$U/MarioPartyRD/Party Board/config.json"
[ -f "$cfg" ] || echo '{"video.enableVsync": false, "backend.skipBootSequence": false}' > "$cfg"
cd "$U"
WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-1} XDG_DATA_HOME=$U HOME=$U timeout -s KILL "$secs" "$W/build/linux-default-relwithdebinfo/partyboard" "$@" > "$U/out.log" 2>&1
echo "exit $?"
