#!/bin/bash
# pair.sh <tag> <secs>: N56's pair.sh on w06 (host sets the Room board via PB_N77_BOARD=5) with N84's Bowser-minigame
# force (PB_N84=<turn>) on both peers. GARGS / HARGS: extra args for guest / host (e.g. --desync-at N).
tag=$1; secs=$2
W=/home/ryha/repos/partyboard-slots/s2
R=$W/build/n84/run/$tag
rm -rf $R; mkdir -p $R/x
unshare -rn sleep 100000 & NS=$!
sleep 0.5
nsenter -t $NS -U -n --preserve-credentials sh -c 'ip link set lo up; ip link add d0 type dummy; ip addr add 10.84.0.1/24 dev d0; ip link set d0 up'
for p in host guest; do
  C="$R/$p/data/MarioPartyRD/Party Board"; mkdir -p "$C"
  echo "{\"video.enableVsync\": false, \"backend.skipBootSequence\": true}" > "$C/config.json"
  echo '{"relay": "off", "name": "'$p'"}' > "$C/online.json"
done
cd $W/build/linux-default-relwithdebinfo
pids=
for p in host guest; do
  if [ $p = host ]; then a=$HARGS; else a=$GARGS; fi
  nsenter -t $NS -U -n --preserve-credentials env XDG_DATA_HOME=$R/$p/data HOME=$R/$p PB_N38=$p PB_N38_DIR=$R/x \
    PB_N77_BOARD=5 PB_N84=${N84TURN:-1} $ENVX $( [ $p = guest ] && echo $GENV ) \
    timeout -s KILL $secs ./partyboard --hash-log $R/$p.hash $a > $R/$p.out 2>&1 < /dev/null &
  pids="$pids $!"; sleep 1
done
wait $pids
kill $NS
echo done $tag
