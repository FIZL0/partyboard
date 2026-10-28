#!/bin/bash
# run.sh <build base|new> <tag> <script> [args]: one --script turbo run with hash log in its own user dir
H=/home/ryha/repos/partyboard-slots/s2/build/n85; b=$1; t=$2; s=$3; shift 3
R=$H/runs/$b-$t; rm -rf $R; mkdir -p "$R/data/MarioPartyRD/Party Board"
[ -n "$SKIPBOOT" ] && printf '{\n  "backend.wasPresetChosen": true,\n  "backend.skipBootSequence": true\n}\n' > "$R/data/MarioPartyRD/Party Board/config.json"
cd $H/$b
XDG_DATA_HOME=$R/data HOME=$R env $ENVX timeout -s KILL ${TMO:-300} ./partyboard --script $s --turbo --hash-log $R/h.hash "$@" > $R/out 2>&1
echo "$b-$t: $(grep -c . $R/h.hash) frames, $(grep -m1 'script ended\|Ovl Return' $R/out)"
