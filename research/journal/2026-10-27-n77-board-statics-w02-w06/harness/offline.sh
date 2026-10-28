#!/bin/bash
# offline.sh <tag>: the offline check. mash-a (w01 party, 17400 frames) and mash-a with --match 5@300 (w06), each with
# its own user dir, hash logs in build/n77/off/<tag>-{mash,w06}.hash.
W=/home/ryha/repos/partyboard-slots/s2
O=$W/build/n77/off; mkdir -p $O
cd $W/build/linux-default-relwithdebinfo
for k in mash w06; do
  U=$O/$1-$k; rm -rf $U; C="$U/data/MarioPartyRD/Party Board"; mkdir -p "$C"
  echo '{"video.enableVsync": false, "backend.skipBootSequence": true}' > "$C/config.json"
  a=""; [ $k = w06 ] && a="--match 5@300"
  XDG_DATA_HOME=$U/data HOME=$U timeout -s KILL 900 ./partyboard --script $W/tools/netplay/mash-a.script --turbo \
    --hash-log $O/$1-$k.hash $a > $O/$1-$k.out 2>&1 < /dev/null &
done
wait
