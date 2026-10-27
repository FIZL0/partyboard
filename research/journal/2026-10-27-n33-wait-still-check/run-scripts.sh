# N33 scratch scripts (three files run.sh, variant.sh, perf.sh concatenated; split before use). Not built or run by check.sh.
#!/bin/bash
# usage: run.sh <tag> [extra env...]  -- starts host + game; control via $S/<tag>.fifo
S=${S:?set S to a scratch dir holding host (gcc scratch_host.c ... libpbnet.a)}
tag=$1; shift; lock=${LOCK:-false}
U=$S/u-$tag; rm -rf $U $S/$tag.code $S/$tag.fifo; mkdir -p "$U/MarioPartyRD/Party Board"
echo "{\"video.enableVsync\": false, \"backend.skipPreLaunchUI\": true, \"backend.skipBootSequence\": true, \"video.lockAspectRatio\": $lock, \"video.enableFullscreen\": ${FS:-false}}" > "$U/MarioPartyRD/Party Board/config.json"
mkfifo $S/$tag.fifo
(exec sleep 100001 > $S/$tag.fifo &)
$S/host $S/$tag.code n33build < $S/$tag.fifo > $S/$tag.host.log 2>&1 &
hostpid=$!
cd /home/ryha/repos/partyboard-slots/s1
env "$@" N33_CODE=$S/$tag.code N33_BUILD=n33build XDG_DATA_HOME=$U timeout -s KILL ${DUR:-90} gamescope --backend headless -w ${GW:-1280} -h ${GH:-960} -W ${OW:-1280} -H ${OH:-960} -- build/linux-default-relwithdebinfo/partyboard > $S/$tag.game.log 2>&1
kill $hostpid; pkill -x -f "sleep 100001"
#!/bin/bash
# variant.sh <tag> [env...]: full WAIT check, shots in $S/<tag>-*.png
S=${S:?set S to a scratch dir holding host (gcc scratch_host.c ... libpbnet.a)}
tag=$1
(DUR=75 $S/run.sh "$@" > $S/$tag.run.log 2>&1 &)
for i in $(seq 60); do grep -q "board overlay" $S/$tag.game.log 2>/dev/null && break; sleep 1; done
sleep 12
$S/shot.sh $S/$tag-0before.png
echo "stall 6000" > $S/$tag.fifo
sleep 0.5; $S/shot.sh $S/$tag-1wait05.png
sleep 1.5; $S/shot.sh $S/$tag-2wait2.png
sleep 2.5; $S/shot.sh $S/$tag-3wait45.png
sleep 3; $S/shot.sh $S/$tag-4after.png
sleep 1; $S/shot.sh $S/$tag-5after2.png
grep "N33 first\|N33 600" $S/$tag.game.log | tail -3
#!/bin/bash
# perf.sh <tag> [env...]: turbo --script run with --match 0@900, 70 s
S=${S:?set S to a scratch dir holding host (gcc scratch_host.c ... libpbnet.a)}
tag=$1; shift
U=$S/u-$tag; rm -rf $U; mkdir -p "$U/MarioPartyRD/Party Board"
echo '{"video.enableVsync": false, "backend.skipBootSequence": true}' > "$U/MarioPartyRD/Party Board/config.json"
cd /home/ryha/repos/partyboard-slots/s1
env "$@" N33_TURBO=1 XDG_DATA_HOME=$U timeout -s KILL 70 gamescope --backend headless -w 1280 -h 960 -W 1280 -H 960 -- build/linux-default-relwithdebinfo/partyboard --script tools/netplay/mash-a.script --match 0@900 > $S/$tag.log 2>&1
grep "N33 600" $S/$tag.log | awk '{print $4}' | tr '\n' ' '; echo
