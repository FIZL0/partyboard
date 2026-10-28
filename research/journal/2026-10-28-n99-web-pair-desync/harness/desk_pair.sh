#!/bin/bash
# N99 desk_pair.sh <name> [secs]: the desktop pair through the real Online menu, as web_pair.mjs's tabs (N89's
# run.sh and n89_test.inc: apply N89's ui-hook.diff by hand, it has no hunk headers). Copy 1 hosts, copy 2 joins
# 3 s later, so the two sat at bootDll's title for different times when the host starts the match from there.
# Both write --hash-log; compare from the match's frame 0 (split at "^0 ").
H=$(cd "$(dirname "$0")" && pwd); S=$(cd "$H/../../../.." && pwd); D=$S/build/n99/$1; T=${2:-200}
rm -rf "$D"; U="$D/xdg/MarioPartyRD/Party Board"; mkdir -p "$U"
echo '{"video.enableVsync": false}' > "$U/config.json"
echo '{"relay": "off", "players": [{"name": "Hosty", "key": "HOSTKEY"}]}' > "$U/online.json"
echo '{"relay": "off", "players": [{"name": "Guesty", "key": "GUESTKEY"}]}' > "$U/online-2.json"
sed "s|@D@|$D|g; s|@U@|$U|g" "$H/desk_host.txt" > "$D/host.txt"
sed "s|@D@|$D|g; s|@U@|$U|g" "$H/desk_guest.txt" > "$D/guest.txt"
cd "$S/build/linux-default-relwithdebinfo"
envs=(XDG_DATA_HOME="$D/xdg" HOME="$D")
env "${envs[@]}" PB_N89_TEST="$D/host.txt" timeout -s KILL $T ./partyboard --hash-log "$D/host.hash" > "$D/host.log" 2>&1 &
h=$!
sleep 3
env "${envs[@]}" PB_N89_TEST="$D/guest.txt" timeout -s KILL $T ./partyboard --hash-log "$D/guest.hash" > "$D/guest.log" 2>&1 &
g=$!
wait $h $g
grep -h "lobby started\|killed .* menu\|desync" "$D/host.log" | sed 's/^/host  /'
grep -h "lobby started\|killed .* menu\|desync" "$D/guest.log" | sed 's/^/guest /'
