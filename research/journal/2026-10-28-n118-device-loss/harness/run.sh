#!/bin/bash
# run.sh NAME [PB_LIFECYCLE_TEST]  -> build/n118-work/NAME/{out.log,hash.log}
W=/home/ryha/repos/partyboard-slots/s1/build/n118-work
name=$1; steps=$2
d=$W/$name; rm -rf $d; mkdir -p $d/home $d/xdg
cd /home/ryha/repos/partyboard-slots/s1
export HOME=$d/home XDG_DATA_HOME=$d/xdg
if [ -n "$steps" ]; then export PB_LIFECYCLE_TEST="$steps"; fi
timeout -s KILL ${TMO:-300} nice -n 5 build/linux-default-relwithdebinfo/partyboard --script $W/mash2400.script --turbo --hash-log $d/hash.log $EXTRA > $d/out.log 2>&1
echo "exit $?" >> $d/out.log
