#!/bin/bash
# N142 pair.sh <name>: N122's pair.sh (N89's two copies, one user dir, host + guest by code through the launcher's
# Online, --hash-log on each), with n142_test.inc hooked into ui.cpp (apply.py). The host's audio goes to
# $D/host.raw (SDL disk driver, S16LE stereo 44100), the guest has no audio device. Out: build/n142/<name>/.
H=$(cd "$(dirname "$0")" && pwd); S=$(cd "$H/../../../.." && pwd); W=$S/build/n142; name=$1
D=$W/$name; rm -rf "$D"; U="$D/xdg/MarioPartyRD/Party Board"; mkdir -p "$U"
echo '{"relay": "off", "players": [{"name": "Hosty", "key": "SHAREDKEY"}]}' > "$U/online.json"
echo '{"video.enableVsync": false}' > "$U/config.json"
sed "s|@D@|$D|g; s|@U@|$U|g" "$H/${HOST_SCRIPT:-host.txt}" > "$D/host.txt"
sed "s|@D@|$D|g; s|@U@|$U|g" "$H/${GUEST_SCRIPT:-guest.txt}" > "$D/guest.txt"
cd "$S/build/linux-default-relwithdebinfo"
envs=(XDG_DATA_HOME="$D/xdg" HOME="$D")
env "${envs[@]}" SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE="$D/host.raw" PB_N89_TEST="$D/host.txt" \
  timeout -s KILL 600 ./partyboard --hash-log "$D/host.hash" > "$D/host.log" 2>&1 &
p1=$!
if [ -z "$SOLO" ]; then
  sleep 3
  env "${envs[@]}" SDL_AUDIO_DRIVER=none PB_N89_TEST="$D/guest.txt" timeout -s KILL 600 ./partyboard \
    --hash-log "$D/guest.hash" > "$D/guest.log" 2>&1 &
  p2=$!
fi
wait $p1 ${p2:-}
grep -h "n89:\|n142:\|boot:\|netplay: \(session\|match\|lobby\)" "$D/host.log" | sed 's/^/host  /' | cut -c1-300
[ -z "$SOLO" ] && grep -h "n89:\|n142:\|boot:\|netplay: \(session\|match\|lobby\)" "$D/guest.log" | sed 's/^/guest /' | cut -c1-300
exit 0
