#!/bin/bash
# rejoin.sh <tag> <player>: host + guest; at the host's turn point of <player> (turn 1) the guest is SIGKILLed and
# restarts with N38's rejoin role (Online > Join > Rejoin Match): the host reloads mid-board, the guest fresh.
# N74: N57's, with N74_LOG=<peer>.alloc (instrument.diff's alloc/free log) instead of N57_DUMP; run dir build/n74run/<tag>.
tag=$1; pl=${2:-1}
W=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
R=$W/build/n74run/$tag
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
  nsenter -t $NS -U -n --preserve-credentials env XDG_DATA_HOME=$R/$1/data HOME=$R/$1 PB_N38=$2 PB_N38_DIR=$R/x N74_LOG=$R/$4.alloc \
    timeout -s KILL $3 ./partyboard --hash-log $R/$4.hash > $R/$4.out 2>&1 < /dev/null &
}
run host host 420 host; H=$!
sleep 1
run guest guest 400 guest1
for i in $(seq 1 3000); do grep -q "resync point at frame [0-9]*: turn 1/20, player $pl" $R/host.out && break; sleep 0.1; done
sleep 3
pkill -KILL -f "hash-log $R/guest1.hash"
echo "guest killed: $(grep 'resync point' $R/host.out | tail -1)"
sleep 3
run guest rejoin 300 guest2; G=$!
wait $H $G # not a bare wait: the netns holder never ends
kill $NS
echo done $tag
