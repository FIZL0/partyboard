#!/bin/bash
# regenerate 0012 from the snapshot, then reverse-check every patch on its own (as cmake/aurora_patches.cmake does)
S=${N118_SNAPSHOT:?dir holding lib/ and include/ of extern/aurora with patches 0001-0011 applied}
cd /home/ryha/repos/partyboard-slots/s1/extern/aurora
out=../../cmake/patches/aurora/0012-device-loss-recovery.patch
: > $out
for f in $(cd $S/base && find lib include -type f | sort); do
  if ! cmp -s $S/base/$f $f; then echo "diff --git a/$f b/$f" >> $out; diff -u --label a/$f --label b/$f $S/base/$f $f >> $out; fi
done
rc=0
for p in ../../cmake/patches/aurora/*.patch; do
  if ! git apply --reverse --check $p 2>$S/err; then echo "FAIL $(basename $p)"; cat $S/err; rc=1; fi
done
exit $rc
