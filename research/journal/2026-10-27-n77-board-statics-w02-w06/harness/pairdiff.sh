#!/bin/bash
# pairdiff.sh <run dir>: staticdiff.py of the guest's match-2 dumps against host B's at the same frame and overlay
# (src/game symbols only), minus N68's always-different list (REL function pointers, engine bookkeeping).
R=$1
B=/home/ryha/repos/partyboard-slots/s2/build/linux-default-relwithdebinfo
H=$(dirname "$0")
SKIP='ARQueBuf|DemoCurrentBuffer|HeapTbl|Hu3DData |HuSprData |HuSprGrpData|Hu3DMotion|DrawObjData|SLSerialNo|VCounter|arqIdx|boardLightResetHook|boardLightSetHook|createFunc|destroyFunc|font_tex|landEventFunc|walkEventFunc|walkMiniEventFunc|mesCharCnt|spaceHiliteTex|spaceTex |DVD|dvd|boardTurnFunc|boardStarShowNextHook|boardTurnStartFunc|boardStarGiveHook|boardBowserHook'
for hb in $R/hostB.dump/*.bin; do
  key=$(basename $hb | cut -d- -f2-)
  g=$(ls $R/guest.dump/*-$key 2>/dev/null | tail -1)
  [ -n "$g" ] || continue
  echo "--- $key"
  python3 $H/staticdiff.py $B/libdol.so $g $hb $B/CMakeFiles/dol.dir/src/game | grep -Ev "$SKIP"
done
