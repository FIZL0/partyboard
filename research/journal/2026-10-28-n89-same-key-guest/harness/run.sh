#!/bin/bash
# run.sh <name> [samekey]: two copies of the game with ONE user dir (as two copies on one PC). Copy 1 hosts, copy 2
# joins by the host's code. samekey: copy 2's online-2.json is pre-written with the host's key too (as if it had
# shared online.json), so the lobby's same-key path is what seats it. Logs in $W/<name>/{host,guest}.log.
H=$(cd "$(dirname "$0")" && pwd); S=$(cd "$H/../../../.." && pwd); W=${N89_WORK:-$S/build/n89-work}; name=$1
D=$W/$name; rm -rf "$D"; U="$D/xdg/MarioPartyRD/Party Board"; mkdir -p "$U"
echo '{"relay": "off", "players": [{"name": "Hosty", "key": "SHAREDKEY"}]}' > "$U/online.json"
if [ "$2" = samekey ]; then cp "$U/online.json" "$U/online-2.json"; fi
sed "s|@D@|$D|g; s|@U@|$U|g" "$H/host.txt" > "$D/host.txt"
sed "s|@D@|$D|g; s|@U@|$U|g" "$H/guest.txt" > "$D/guest.txt"
cd "$S/build/linux-default-relwithdebinfo"
envs=(XDG_DATA_HOME="$D/xdg" HOME="$D")
env "${envs[@]}" PB_N89_TEST="$D/host.txt" timeout -s KILL 400 ./partyboard > "$D/host.log" 2>&1 &
sleep 3
env "${envs[@]}" PB_N89_TEST="$D/guest.txt" timeout -s KILL 400 ./partyboard > "$D/guest.log" 2>&1 &
wait
grep -h "n89:\|n88: frame .* tap\|tap target" "$D/host.log" | sed 's/^/host  /' | cut -c1-600
grep -h "n89:\|n88: frame .* tap\|tap target" "$D/guest.log" | sed 's/^/guest /' | cut -c1-600
