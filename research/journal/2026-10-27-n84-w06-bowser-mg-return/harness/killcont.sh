#!/bin/bash
# killcont.sh <tag>: N56's kill/Continue on w06 with N84's force. Host + guest play into Bowser's minigame (m457/m458),
# the host is SIGKILLed $KILLDELAY s after the minigame starts, the guest presses Leave Match, the host relaunches
# (same user dir) and hosts the w06 save from the Continue tab, the guest joins with the new code.
tag=$1
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
run() { # user role secs log
  nsenter -t $NS -U -n --preserve-credentials env XDG_DATA_HOME=$R/$1/data HOME=$R/$1 PB_N38=$2 PB_N38_DIR=$R/x \
    PB_N77_BOARD=5 PB_N84=1 PB_N84_SAVE="Bowser*" \
    timeout -s KILL $3 ./partyboard --hash-log $R/$4.hash > $R/$4.out 2>&1 < /dev/null &
  echo $!
}
H1=$(run host host 300 host1)
sleep 1
G=$(run guest guest 600 guest)
for i in $(seq 1 3000); do grep -q "Bowser minigame ovl" $R/host1.out && break; sleep 0.1; done
sleep ${KILLDELAY:-10}
kill -KILL $H1; pkill -KILL -f "hash-log $R/host1.hash"
echo "host killed: $(grep 'resync point\|Bowser minigame' $R/host1.out | tail -2)"
sleep 6
touch $R/x/cmd-leave
for i in $(seq 1 300); do grep -q "n56: match left" $R/guest.out && break; sleep 0.1; done
echo "guest: $(grep -E 'match left' $R/guest.out | tail -1)"
H2=$(run host conthost ${SECS2:-240} host2)
sleep ${SECS2:-240}
kill -KILL $G; pkill -KILL -f "hash-log $R/guest.hash"
kill $NS
echo done $tag
