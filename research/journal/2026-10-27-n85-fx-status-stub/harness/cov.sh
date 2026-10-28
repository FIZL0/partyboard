#!/bin/bash
# cov.sh <build> <tag> <script> <loc>...: gdb hit counts of each location in one run (env from $ENVX, $SKIPBOOT; game args $ARGS)
H=/home/ryha/repos/partyboard-slots/s2/build/n85; b=$1; t=$2; s=$3; shift 3
R=$H/runs/cov-$b-$t; rm -rf $R; mkdir -p "$R/data/MarioPartyRD/Party Board"
[ -n "$SKIPBOOT" ] && printf '{\n  "backend.wasPresetChosen": true,\n  "backend.skipBootSequence": true\n}\n' > "$R/data/MarioPartyRD/Party Board/config.json"
G=$R/cmds.gdb; { echo "set pagination off"; echo "set breakpoint pending on"; echo "handle SIGPIPE SIGUSR1 SIGUSR2 nostop noprint"
for l in "$@"; do echo "break $l"; echo "commands"; echo "silent"; echo "continue"; echo "end"; done
echo "run"; echo "info breakpoints"; echo "kill"; echo "quit"; } > $G
cd $H/$b
XDG_DATA_HOME=$R/data HOME=$R env $ENVX timeout -s KILL ${TMO:-500} gdb -q -batch -x $G --args ./partyboard --script $s --turbo $ARGS > $R/out 2>&1
grep -E "^[0-9]+ +breakpoint|already hit" $R/out
