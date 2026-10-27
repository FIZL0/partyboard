#!/bin/sh
# peer.sh <host|guest> <tag>: run one peer inside the test netns, detached; its user dir is $R/<peer>
R=/home/ryha/repos/partyboard-slots/s1/build/n42run
NS=$(cat $R/ns.pid)
cd /home/ryha/repos/partyboard-slots/s1/build/linux-default-relwithdebinfo
setsid nsenter -t $NS -U -n --preserve-credentials env XDG_DATA_HOME=$R/$1/data HOME=$R/$1 PB_N42=$1 PB_N42_DIR=$R/x $PB_EXTRA \
  timeout -s KILL 1800 ./partyboard --hash-log $R/$1.$2.hash > $R/$1.$2.out 2>&1 < /dev/null &
echo $!
