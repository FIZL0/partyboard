#!/bin/bash
# run.sh <name> <secs> [skipboot] -- partyboard args...   (cwd: worktree)
name=$1; secs=$2; shift 2
skip=false; [ "$1" = skipboot ] && { skip=true; shift; }
[ "$1" = -- ] && shift
W=/home/ryha/repos/partyboard-slots/s2
U=$W/.n47-runs/$name
mkdir -p "$U/MarioPartyRD/Party Board"
cfg="$U/MarioPartyRD/Party Board/config.json"
[ -f "$cfg" ] || echo "{\"video.enableVsync\": false, \"backend.skipBootSequence\": $skip}" > "$cfg"
cd $U
XDG_DATA_HOME=$U HOME=$U PB_TURBO=1 timeout -s KILL $secs $W/build/linux-default-relwithdebinfo/partyboard "$@" > $U/out.log 2>&1
echo "exit $?"
