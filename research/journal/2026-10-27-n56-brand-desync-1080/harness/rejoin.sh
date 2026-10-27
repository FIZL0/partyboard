#!/bin/bash
# rejoin.sh <tag>: match 1 (host A + guest, both --leave-at 1500), then a fresh host B and the same guest play match 2
tag=$1
W=/home/ryha/repos/partyboard-slots/s2
R=$W/.n56-runs/$tag
rm -rf $R; mkdir -p $R/x
unshare -rn sleep 100000 & NS=$!
sleep 0.5
nsenter -t $NS -U -n --preserve-credentials sh -c 'ip link set lo up; ip link add d0 type dummy; ip addr add 10.77.0.1/24 dev d0; ip link set d0 up'
for p in hostA hostB guest; do
  C="$R/$p/data/MarioPartyRD/Party Board"; mkdir -p "$C"
  echo "{\"video.enableVsync\": false, \"backend.skipBootSequence\": true}" > "$C/config.json"
  echo '{"relay": "off", "name": "'$p'"}' > "$C/online.json"
done
cd $W/build/linux-default-relwithdebinfo
run() { # name role secs args
  nsenter -t $NS -U -n --preserve-credentials env XDG_DATA_HOME=$R/$1/data HOME=$R/$1 PB_N38=$2 PB_N38_DIR=$R/x PB_N56=1 \
    timeout -s KILL $3 ./partyboard --hash-log $R/$1.hash $4 > $R/$1.out 2>&1 < /dev/null &
}
run hostA host 200 "--leave-at ${LEAVE_A:-1500}"; A=$!
sleep 1
run guest guest 200 "--leave-at ${LEAVE_G:-1500}"; G=$!
for i in $(seq 1 1200); do grep -q "match left" $R/hostA.out && break; sleep 0.1; done
sleep 1; kill -KILL $A; pkill -KILL -f "hash-log $R/hostA.hash"
echo "host A gone"
run hostB host 120 ""; B=$!
wait $B; kill -KILL $G 2>/dev/null; pkill -KILL -f "hash-log $R/guest.hash"
kill $NS
echo done $tag
