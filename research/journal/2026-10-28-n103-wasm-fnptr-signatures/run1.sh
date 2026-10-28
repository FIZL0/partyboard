#!/bin/bash
# run1.sh <gamedir> <outdir> <ovl> [extra env...]: one boot-hook minigame run, all COM
SP=$(cd "$(dirname "$0")" && pwd); g=$1; d=$2; ovl=$3
rm -rf $d; mkdir -p "$d/xdg/MarioPartyRD/Party Board"; cp $SP/cfg.json "$d/xdg/MarioPartyRD/Party Board/config.json"
cd $g && PB_N73_OVL=$ovl XDG_DATA_HOME=$d/xdg HOME=$d timeout -s KILL ${LIM:-240} ./partyboard --script $SP/m.script --turbo --hash-log $d/hash.log > $d/out.txt 2>&1
echo "rc $?" >> $d/out.txt
