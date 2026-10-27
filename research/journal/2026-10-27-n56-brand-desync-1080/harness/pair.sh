#!/bin/bash
# pair.sh <tag> <secs> [vsync true|false]: host + guest lobby match in a private netns, hash logs + n56 brand trace
tag=$1; secs=$2; vs=${3:-false}
W=/home/ryha/repos/partyboard-slots/s2
R=$W/.n56-runs/$tag
rm -rf $R; mkdir -p $R/x
unshare -rn sleep 100000 & NS=$!
sleep 0.5
nsenter -t $NS -U -n --preserve-credentials sh -c 'ip link set lo up; ip link add d0 type dummy; ip addr add 10.77.0.1/24 dev d0; ip link set d0 up'
for p in host guest; do
  C="$R/$p/data/MarioPartyRD/Party Board"; mkdir -p "$C"
  echo "{\"video.enableVsync\": $vs, \"backend.skipBootSequence\": ${SKIP:-true}}" > "$C/config.json"
  echo '{"relay": "off", "name": "'$p'"}' > "$C/online.json"
done
cd $W/build/linux-default-relwithdebinfo
for p in host guest; do
  nsenter -t $NS -U -n --preserve-credentials env XDG_DATA_HOME=$R/$p/data HOME=$R/$p PB_N38=$p PB_N38_DIR=$R/x PB_N56=1 \
    $PFX timeout -s KILL $secs ./partyboard --hash-log $R/$p.hash $( [ $p = guest ] && echo $GARGS ) > $R/$p.out 2>&1 < /dev/null &
  pids="$pids $!"; sleep 1
done
wait $pids
kill $NS
echo done $tag
