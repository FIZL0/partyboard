#!/bin/sh
# wait.sh <dir> <secs>: until both peers log "results are over" or a desync, or time runs out
D=$1; for i in $(seq 1 $2); do
  if grep -aq "results are over" $D/host.out && grep -aq "results are over" $D/guest.out; then echo done; exit 0; fi
  if grep -aq "desync at" $D/host.out $D/guest.out; then echo desync; exit 0; fi
  sleep 1; done; echo timeout
