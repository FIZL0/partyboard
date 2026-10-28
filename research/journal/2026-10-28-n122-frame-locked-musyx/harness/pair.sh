#!/bin/bash
# pair.sh <name>: N89's two copies through the real lobby (one user dir, host + guest by code; needs n89_test.inc hooked
# into src/port/ui/ui.cpp as N89's ui-hook.diff does), with --hash-log on each, the host on SDL's dummy audio driver
# and the guest with no device. Both quit at UI frame 5000 (match started at ~950, board intro, first turn's wait).
H=$(cd "$(dirname "$0")" && pwd); S=$(cd "$H/../../../.." && pwd); W=$S/build/n122/pair; name=$1
D=$W/$name; rm -rf "$D"; U="$D/xdg/MarioPartyRD/Party Board"; mkdir -p "$U"
echo '{"relay": "off", "players": [{"name": "Hosty", "key": "SHAREDKEY"}]}' > "$U/online.json"
echo '{"video.enableVsync": false}' > "$U/config.json"
sed "s|@D@|$D|g; s|@U@|$U|g" "$H/host.txt" > "$D/host.txt"
sed "s|@D@|$D|g; s|@U@|$U|g" "$H/guest.txt" > "$D/guest.txt"
cd "$S/build/linux-default-relwithdebinfo"
envs=(XDG_DATA_HOME="$D/xdg" HOME="$D")
env "${envs[@]}" SDL_AUDIO_DRIVER=dummy PB_N89_TEST="$D/host.txt" timeout -s KILL 600 ./partyboard --hash-log "$D/host.hash" > "$D/host.log" 2>&1 &
p1=$!
sleep 3
env "${envs[@]}" SDL_AUDIO_DRIVER=none PB_N89_TEST="$D/guest.txt" timeout -s KILL 600 ./partyboard --hash-log "$D/guest.hash" > "$D/guest.log" 2>&1 &
p2=$!
wait $p1 $p2
grep -h "n89:\|MusyX on\|netplay: \(session\|match\)" "$D/host.log" | sed 's/^/host  /' | cut -c1-300
grep -h "n89:\|MusyX on\|netplay: \(session\|match\)" "$D/guest.log" | sed 's/^/guest /' | cut -c1-300
