#!/bin/bash
# edge.sh <tag> <frame>...: one-process --script mash-a runs with --match 0@<frame>, in parallel; prints each run's
# match-state frame and board start frame.
tag=$1; shift
W=/home/ryha/repos/partyboard-slots/s3
cd $W/build/linux-default-relwithdebinfo
for f in "$@"; do
  R=$W/build/n67run/$tag/m$f; rm -rf $R; mkdir -p $R/u
  XDG_DATA_HOME=$R/u HOME=$R/u timeout -s KILL ${SECS:-60} ./partyboard --script ../../tools/netplay/mash-a.script --turbo \
    --match 0@$f --hash-log $R/h > $R/out 2>&1 < /dev/null &
done
wait
for f in "$@"; do
  R=$W/build/n67run/$tag/m$f
  echo "$f: $(grep -o 'match state set at frame [0-9]*' $R/out) / $(grep -o 'board overlay [0-9]* starts at frame [0-9]*' $R/out | head -1)"
done
