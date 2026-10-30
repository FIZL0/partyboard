#!/bin/bash
# N150: every shot run, one after another (AGENTS.md: no fan-out). batch.sh <out root>
J=$(dirname "$0"); R=$1
mkdir -p "$R"
Z=$R/z.script
{ echo "end 2400"; for f in 300 420 900; do echo "$f 0 Z"; echo "$((f + 6)) 0 none"; done; } >"$Z"
for fam in gamecube xbox playstation keyboard; do
    for ovl in 13 26 40 66; do
        DELAYS=60,200,400,800 $J/run.sh "$R/ovl$ovl-$fam" $fam $ovl
    done
    DELAYS=30,120,300 $J/run.sh "$R/inst9-$fam" $fam 9 inst
    DELAYS=10,60,200 $J/run.sh "$R/practice9-$fam" $fam 9 inst "$Z"
    for ovl in 72 75 82; do
        DELAYS=30,120,300 $J/run.sh "$R/ovl$ovl-$fam" $fam $ovl
    done
done
