#!/bin/sh
D=$1
for w in host guest; do echo "== $w: $(tail -1 $D/$w.hash | cut -d' ' -f1,3)"; grep -a "match over\|desync at\|results are over\|back in the lobby\|n42: .* ui \|match start\|lobby state" $D/$w.out | tail -${2:-6} | cut -c1-220; done
