#!/bin/sh
# run.sh <name> <timeout s> args...: one scripted partyboard run with its own user dir under the worktree
N=$1; T=$2; shift 2
R=/home/ryha/repos/partyboard-slots/s1/build/n42b/$N
mkdir -p "$R/data/MarioPartyRD/Party Board"
[ -f "$R/data/MarioPartyRD/Party Board/config.json" ] || echo '{"video.enableVsync": false}' > "$R/data/MarioPartyRD/Party Board/config.json"
cd /home/ryha/repos/partyboard-slots/s1/build/linux-default-relwithdebinfo
env XDG_DATA_HOME=$R/data HOME=$R $PB_EXTRA timeout -s KILL $T ./partyboard --turbo --hash-log $R/hash.log "$@" > $R/out.log 2>&1 < /dev/null
echo "exit $?"
