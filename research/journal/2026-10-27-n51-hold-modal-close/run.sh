#!/bin/bash
# run.sh <tag> [env...]: host + game in gamescope headless, a 6 s stall once the board is up, screenshots
S=${S:?set S to a scratch dir holding host (gcc scratch_host.c from the N33 journal + libpbnet.a)}
tag=$1; shift
U=$S/u-$tag; rm -rf $U $S/$tag.code $S/$tag.fifo $S/$tag-*.png; mkdir -p "$U/MarioPartyRD/Party Board"
echo '{"video.enableVsync": false, "backend.skipPreLaunchUI": true, "backend.skipBootSequence": true}' > "$U/MarioPartyRD/Party Board/config.json"
mkfifo $S/$tag.fifo
(exec sleep 100002 > $S/$tag.fifo &)
$S/host $S/$tag.code n51build < $S/$tag.fifo > $S/$tag.host.log 2>&1 &
hostpid=$!
cd /home/ryha/repos/partyboard-slots/s2
before=$(ls /run/user/$UID | grep '^gamescope-' | sort -V | tail -1)
env "$@" N33_CODE=$S/$tag.code N33_BUILD=n51build XDG_DATA_HOME=$U HOME=$U timeout -s KILL 80 gamescope --backend headless -w 1280 -h 960 -W 1280 -H 960 -- build/linux-default-relwithdebinfo/partyboard > $S/$tag.game.log 2>&1 &
sleep 3
gs=$(ls -t /run/user/$UID | grep '^gamescope-[0-9]*$' | head -1)
shot() { GAMESCOPE_WAYLAND_DISPLAY=$gs gamescopectl screenshot "$1" >/dev/null 2>&1; }
for i in $(seq 70); do grep -q "entering board" $S/$tag.game.log 2>/dev/null && break; sleep 1; done
sleep 12
shot $S/$tag-0before.png
echo "stall 6000" > $S/$tag.fifo
sleep 4.5; shot $S/$tag-1wait.png
sleep 4; shot $S/$tag-2after.png
sleep 2; shot $S/$tag-3after.png
sleep 2
kill -9 $hostpid; pkill -f "sleep 100002"; pkill -9 -f "XDG_DATA_HOME=$U" ; wait
