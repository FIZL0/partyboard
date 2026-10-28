#!/bin/bash
# run.sh <name> [lifecycle steps]: N89's two copies of the game with one user dir; copy 1 hosts, copy 2 joins by the
# host's code and starts the match. The guest gets PB_LIFECYCLE_TEST (default: background at loop 1500, foreground at
# loop 2300, about 13 s held), so the host's "recover" lines show it waited for and why. Build with the ui.cpp hook
# (README.md). Logs in $W/<name>/{host,guest}.log.
H=$(cd "$(dirname "$0")" && pwd); S=$(cd "$H/../../../.." && pwd); W=${N110_WORK:-$S/build/n110-work}; name=$1
steps=${2:-bg@1500,fg@2300}
D=$W/$name; rm -rf "$D"; U="$D/xdg/MarioPartyRD/Party Board"; mkdir -p "$U"
echo '{"relay": "off", "players": [{"name": "Hosty", "key": "HOSTKEY"}]}' > "$U/online.json"
echo '{"relay": "off", "players": [{"name": "Guesty", "key": "GUESTKEY"}]}' > "$U/online-2.json"
sed "s|@D@|$D|g; s|@U@|$U|g" "$H/host.txt" > "$D/host.txt"
sed "s|@D@|$D|g; s|@U@|$U|g" "$H/guest.txt" > "$D/guest.txt"
cd "$S/build/linux-default-relwithdebinfo"
envs=(XDG_DATA_HOME="$D/xdg" HOME="$D")
env "${envs[@]}" PB_N89_TEST="$D/host.txt" timeout -s KILL 400 stdbuf -oL -eL ./partyboard > "$D/host.log" 2>&1 &
host=$!
sleep 3
env "${envs[@]}" PB_N89_TEST="$D/guest.txt" PB_LIFECYCLE_TEST="$steps" timeout -s KILL 400 stdbuf -oL -eL ./partyboard \
    > "$D/guest.log" 2>&1 &
guest=$!
wait $host $guest
grep -h "n89:\|n110:\|lifecycle\|keeper\|desync\|Desync" "$D/host.log" | sed 's/^/host  /' | cut -c1-400
grep -h "n89:\|n110:\|lifecycle\|keeper\|desync\|Desync" "$D/guest.log" | sed 's/^/guest /' | cut -c1-400
