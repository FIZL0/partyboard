#!/bin/bash
# N83: run.sh <dir> [args]: one m440 run via harness.diff's boot hook (OVL=48, SCRIPT, LIM, GAME, PB_N83_* from env)
S=${S:-$PWD}  # holds cfg.json (skipBootSequence) and tap.script
d=$1; shift
rm -rf $d; mkdir -p "$d/xdg/MarioPartyRD/Party Board"; cp $S/cfg.json "$d/xdg/MarioPartyRD/Party Board/config.json"
PB_N73_OVL=${OVL:-48} XDG_DATA_HOME=$d/xdg HOME=$d timeout -s KILL ${LIM:-300} ${GAME:-$(dirname "$0")/../../../build/linux-default-relwithdebinfo/partyboard} --script ${SCRIPT:-$S/end.script} --turbo --hash-log $d/hash.log "$@" > $d/out.txt 2>&1
