#!/bin/bash
# offline.sh <tag> [args]: one --script mash-a run (turbo, hash log) in its own user dir under build/n84/off/<tag>
W=/home/ryha/repos/partyboard-slots/s2; t=$1; shift
R=$W/build/n84/off/$t; rm -rf $R; mkdir -p "$R/data/MarioPartyRD/Party Board"
cd $W/build/linux-default-relwithdebinfo
XDG_DATA_HOME=$R/data HOME=$R $ENVX timeout -s KILL 200 ./partyboard --script $W/tools/netplay/mash-a.script --turbo --hash-log $R/h.hash "$@" > $R/out 2>&1
echo "$t: $(grep 'script ended' $R/out)"
