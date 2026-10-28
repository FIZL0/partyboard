#!/bin/bash
# N123: regenerate 0013 from a snapshot of extern/aurora's lib/ and include/ with 0001-0012 applied ($N123_SNAPSHOT/base),
# then reverse-check every patch on its own (as cmake/aurora_patches.cmake does). Run from the worktree root.
S=${N123_SNAPSHOT:?dir holding base/lib and base/include}
cd extern/aurora || exit 1
out=../../cmake/patches/aurora/0013-surface-sync-unlocked.patch
: > $out
for f in $(cd $S/base && find lib include -type f | sort); do
  if ! cmp -s $S/base/$f $f; then echo "diff --git a/$f b/$f" >> $out; diff -u --label a/$f --label b/$f $S/base/$f $f >> $out; fi
done
rc=0
for p in ../../cmake/patches/aurora/*.patch; do
  if ! git apply --reverse --check $p 2>$S/err; then echo "FAIL $(basename $p)"; cat $S/err; rc=1; fi
done
exit $rc
