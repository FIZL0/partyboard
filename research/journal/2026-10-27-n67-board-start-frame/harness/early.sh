#!/bin/bash
# early.sh <tag> <host-frame>: host + guest lobby match in a private netns; when the host's hash log reaches
# <host-frame> (before turn 1's first turn point) the guest is SIGKILLed and restarts with the rejoin role
# (Online > Join > Rejoin Match). The host reloads from the match start inside the board, the guest from a menu.
tag=$1; at=${2:-600}
W=/home/ryha/repos/partyboard-slots/s3
R=$W/build/n67run/$tag
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
  nsenter -t $NS -U -n --preserve-credentials env XDG_DATA_HOME=$R/$1/data HOME=$R/$1 PB_N38=$2 PB_N38_DIR=$R/x \
    timeout -s KILL $3 ./partyboard --hash-log $R/$4.hash $GARGS > $R/$4.out 2>&1 < /dev/null &
}
run host host ${SECS:-300} host; H=$!
sleep 1
run guest guest 200 guest1
for i in $(seq 1 3000); do
  grep -q "board overlay" $R/host.out && [ "$(tail -c 2000 $R/host.hash 2>/dev/null | tail -1 | awk '{print $1+0}')" -ge $at ] 2>/dev/null && break
  sleep 0.05
done
pkill -KILL -f "hash-log $R/guest1.hash"
echo "guest killed at host line: $(tail -1 $R/host.hash | cut -c1-60)"
sleep ${GAP:-3}
run guest rejoin ${SECS2:-200} guest2; G=$!
wait $H $G # not a bare wait: the netns holder never ends
kill $NS
echo done $tag
