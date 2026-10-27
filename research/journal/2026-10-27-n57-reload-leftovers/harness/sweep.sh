#!/bin/bash
# sweep.sh <ovl>...: each minigame overlay straight from boot (4 COMs), restarted twice 600 frames in (needs instrument.diff;
# set N57_LEFTOVERS=1 to run the netplay scrub's leftovers_free at each overlay start, N57_DUMP=1 for block traces)
W=/home/ryha/repos/partyboard-slots/s2
for o in "$@"; do
  R=$W/build/n57run/sw/$TAG/$o; rm -rf $R; mkdir -p "$R/data/MarioPartyRD/Party Board"
  echo '{"video.enableVsync": false, "backend.skipBootSequence": true}' > "$R/data/MarioPartyRD/Party Board/config.json"
  (cd $W/build/linux-default-relwithdebinfo && N57_MG=$o N57_RESTART=$o:600:2 XDG_DATA_HOME=$R/data HOME=$R \
    timeout -s KILL 150 ./partyboard --script $(dirname "$(readlink -f "$0")")/mg2.script --turbo > $R/out.txt 2>&1)
  echo "$o: $(grep "n57: ovl $o start" $R/out.txt | sed 's/.*start: //' | tr '\n' '|')"
done
