#!/usr/bin/env bash
# N82: run overlay $1 on the new build under gdb, count hits on breakpoints $2.. (function or file:line)
R=$(cd "$(dirname "$0")" && pwd); ovl=$1; shift; d=$R/runs/gdb-$ovl; frames=${FRAMES:-30000}
rm -rf "$d"; mkdir -p "$d/xdg/MarioPartyRD/Party Board"
cat >"$d/xdg/MarioPartyRD/Party Board/config.json" <<J
{ "backend.isoPath": "$PB_DISC", "backend.wasPresetChosen": true, "backend.skipBootSequence": true, "video.enableVsync": false }
J
echo "end $frames" >"$d/end.script"
{ echo "set breakpoint pending on"; echo "set pagination off"; echo "handle SIGPIPE nostop noprint"
  for b in "$@"; do printf 'break %s\ncommands\nsilent\ncontinue\nend\n' "$b"; done
  echo run; echo "info breakpoints"; } >"$d/cmds.gdb"
args=(-batch -x "$d/cmds.gdb")
PB_N82_OVL=$ovl XDG_DATA_HOME=$d/xdg HOME=$d timeout -s KILL 900 gdb "${args[@]}" --args "$R/new/partyboard" \
    --script "$d/end.script" --turbo >"$d/gdb.txt" 2>&1
grep -E "^[0-9]+ +breakpoint|already hit" "$d/gdb.txt"
