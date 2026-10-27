#!/bin/sh
# peer.sh <host|guest> <role> <tag>: run one peer inside the test netns, detached
R=/home/ryha/repos/partyboard-slots/s3/build/n38run
NS=$(cat $R/ns.pid)
cd /home/ryha/repos/partyboard-slots/s3/build/linux-default-relwithdebinfo
setsid nsenter -t $NS -U -n --preserve-credentials env XDG_DATA_HOME=$R/$1/data HOME=$R/$1 PB_N38=$2 PB_N38_DIR=$R/x \
  ./partyboard --hash-log $R/$1.$3.hash > $R/$1.$3.out 2>&1 < /dev/null &
echo $!
