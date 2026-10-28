#!/bin/bash
# run.sh <name> <script> [nodisc]: one game run with its own user dir, harness script, log in $R/<name>.log
R=$(dirname "$0"); name=$1; script=$2
rm -rf "$R/$name"; mkdir -p "$R/$name/xdg/MarioPartyRD/Party Board"
echo '{"relay": "off", "players": [{"name": "Tester"}]}' > "$R/$name/xdg/MarioPartyRD/Party Board/online.json"
cd "$R/../linux-default-relwithdebinfo"
envs=(XDG_DATA_HOME="$R/$name/xdg" HOME="$R/$name" PB_N88_TEST="$script")
if [ "$3" = nodisc ]; then envs+=(PB_N88_FAKEPICK="$PB_DISC"); unset PB_DISC; fi
env "${envs[@]}" timeout -s KILL 120 ./partyboard > "$R/$name.log" 2>&1
echo "exit $?"
