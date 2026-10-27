#!/bin/bash
# pair.sh <tag> <secs>: host + guest lobby match in a private netns (N57's pair.sh, run dir build/n66run/<tag>).
# Needs the scratch harness applied (not committed): N38's harness/netplay_c.diff (lockstep pads: A every 24 frames,
# START in instDll) and N57's harness/n57_harness.inc included in online.cpp with n38_harness() called first thing
# in update_online_match(). GARGS: more guest args (--desync-at <frame>).
tag=$1; secs=$2
W=$(cd "$(dirname "$0")/../../../.." && pwd)
R=$W/build/n66run/$tag
rm -rf $R; mkdir -p $R/x
unshare -rn sleep 100000 & NS=$!
sleep 0.5
nsenter -t $NS -U -n --preserve-credentials sh -c 'ip link set lo up; ip link add d0 type dummy; ip addr add 10.77.0.1/24 dev d0; ip link set d0 up'
for p in host guest; do
  C="$R/$p/data/MarioPartyRD/Party Board"; mkdir -p "$C"
  echo '{"video.enableVsync": false, "backend.skipBootSequence": true}' > "$C/config.json"
  echo '{"relay": "off", "name": "'$p'"}' > "$C/online.json"
done
cd $W/build/linux-default-relwithdebinfo
for p in host guest; do
  nsenter -t $NS -U -n --preserve-credentials env XDG_DATA_HOME=$R/$p/data HOME=$R/$p PB_N38=$p PB_N38_DIR=$R/x \
    timeout -s KILL $secs ./partyboard --hash-log $R/$p.hash $( [ $p = guest ] && echo $GARGS ) > $R/$p.out 2>&1 < /dev/null &
  pids="$pids $!"; sleep 1
done
wait $pids
kill $NS
echo done $tag
