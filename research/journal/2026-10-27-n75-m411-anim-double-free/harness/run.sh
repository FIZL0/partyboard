#!/bin/bash
# run.sh <tag> <ovl>: one minigame straight from boot (4 COMs), 9000 frames, count free errors
# needs harness/instrument.diff applied (bootDll N75_MG jump, netplay.c N75_RESTART); N75_RESTART=19:600:2 run.sh tag 19
W=/home/ryha/repos/partyboard-slots/s1; S=$(dirname "$(readlink -f "$0")")
R=$W/build/n75run/$1; rm -rf $R; mkdir -p "$R/data/MarioPartyRD/Party Board"
echo '{"video.enableVsync": false, "backend.skipBootSequence": true}' > "$R/data/MarioPartyRD/Party Board/config.json"
(cd $W/build/linux-default-relwithdebinfo && N75_MG=$2 XDG_DATA_HOME=$R/data HOME=$R \
  timeout -s KILL 200 ./partyboard --script $S/mg.script --turbo --hash-log $R/hash.txt > $R/out.txt 2>&1)
echo "$1: $(grep -c 'memory free error' $R/out.txt) free errors; $(tail -1 $R/hash.txt 2>/dev/null)"
