#!/bin/bash
# killcont.sh <tag>: N47's two-peer kill/Continue. Host + guest play to player 2's turn point, the host is SIGKILLed,
# the guest presses Leave Match, the host relaunches (same user dir), hosts the save from the Continue tab, the guest
# joins with the new code. Both resume at player 2's turn.
tag=$1
W=/home/ryha/repos/partyboard-slots/s2
R=$W/.n56-runs/$tag
rm -rf $R; mkdir -p $R/x
unshare -rn sleep 100000 & NS=$!
sleep 0.5
nsenter -t $NS -U -n --preserve-credentials sh -c 'ip link set lo up; ip link add d0 type dummy; ip addr add 10.77.0.1/24 dev d0; ip link set d0 up'
for p in host guest; do
  C="$R/$p/data/MarioPartyRD/Party Board"; mkdir -p "$C"
  echo "{\"video.enableVsync\": false, \"backend.skipBootSequence\": true}" > "$C/config.json"
  echo '{"relay": "off", "name": "'$p'"}' > "$C/online.json"
done
cd $W/build/linux-default-relwithdebinfo
run() { # user role secs log
  nsenter -t $NS -U -n --preserve-credentials env XDG_DATA_HOME=$R/$1/data HOME=$R/$1 PB_N38=$2 PB_N38_DIR=$R/x PB_N56=1 \
    timeout -s KILL $3 ./partyboard --hash-log $R/$4.hash > $R/$4.out 2>&1 < /dev/null &
}
run host host 200 host1
sleep 1
run guest guest 400 guest
for i in $(seq 1 1800); do grep -q "resync point at frame [0-9]*: turn 1/20, player ${KILL_PLAYER:-2}" $R/host1.out && break; sleep 0.1; done
sleep 1
pkill -KILL -f "hash-log $R/host1.hash"
echo "host killed: $(grep 'resync point' $R/host1.out | tail -1)"
sleep 6
touch $R/x/cmd-leave
for i in $(seq 1 300); do grep -q "n56: match left" $R/guest.out && break; sleep 0.1; done
echo "guest: $(grep -E 'match left' $R/guest.out | tail -1)"
run host conthost 150 host2
sleep 150
pkill -KILL -f "hash-log $R/guest.hash"
kill $NS
echo done $tag
