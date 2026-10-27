#!/bin/sh
# pair.sh <tag> <save json> [seconds]: netns + host (Continue tab, the save) + guest (Join with the host's code)
T=$1; SAVE=$2; SECS=${3:-900}
R=/home/ryha/repos/partyboard-slots/s1/build/n64run/pair-$T
P="Party Board"
rm -rf $R; mkdir -p $R/x
for who in host guest; do
  mkdir -p "$R/$who/data/MarioPartyRD/$P/online"
  echo '{"video.enableVsync": false}' > "$R/$who/data/MarioPartyRD/$P/config.json"
done
cp $SAVE "$R/host/data/MarioPartyRD/$P/online/local-0000000050424E54.json"
echo '{"players":[{"name":"Hosty","key":"local-1"}],"local":1,"relay":"off"}' > "$R/host/data/MarioPartyRD/$P/online.json"
setsid -f unshare -rn sleep 100001 < /dev/null > /dev/null 2>&1
sleep 1
NS=$(pgrep -n -f "^sleep 100001$")
echo $NS > $R/ns.pid
IN="nsenter -t $NS -U -n --preserve-credentials"
$IN ip link set lo up; $IN ip link add d0 type dummy; $IN ip addr add 10.77.0.1/24 dev d0; $IN ip link set d0 up
cd /home/ryha/repos/partyboard-slots/s1/build/linux-default-relwithdebinfo
start() {
  setsid $IN env XDG_DATA_HOME=$R/$1/data HOME=$R/$1 PB_N42=$1 PB_N42_DIR=$R/x $PB_EXTRA $2 \
    timeout -s KILL $SECS ./partyboard --hash-log $R/$1.hash > $R/$1.out 2>&1 < /dev/null &
  echo $! > $R/$1.pid
}
start host "$PB_HOST"
for i in $(seq 1 120); do [ -s $R/x/code ] && break; sleep 1; done
CODE=$(cat $R/x/code)
echo "code $CODE"
echo "{\"players\":[{\"name\":\"Guesty\",\"key\":\"guest-key-1\"}],\"local\":1,\"relay\":\"off\",\"last_code\":\"$CODE\"}" > "$R/guest/data/MarioPartyRD/$P/online.json"
start guest "$PB_GUEST"
echo "host $(cat $R/host.pid) guest $(cat $R/guest.pid) ns $NS"
