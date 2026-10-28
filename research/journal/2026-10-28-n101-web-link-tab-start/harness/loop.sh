#!/bin/bash
# N101: four-tab menu runs, each with a hash log
cd "$(dirname "$0")/.."
for i in "$@"; do
  o=build/n101-run$i
  rm -rf $o
  WEB_PORT=$((8870 + 2*i)) PAIR_TABS=4 PAIR_MENU=1 PAIR_ARGS='--hash-log /tmp/h.log' DUMP_FILES=/tmp/h.log \
    timeout -s KILL 900 node tools/web_pair.mjs "$PB_DISC" $o ${SECS:-120} > $o.out 2>&1
  echo "run $i exit $?" >> build/n101-loop.log
done
