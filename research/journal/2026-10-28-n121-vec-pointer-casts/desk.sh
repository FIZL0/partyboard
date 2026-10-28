#!/bin/sh
# desk.sh NAME BOARD: one desktop party.script --match BOARD@1500 --turbo run
R=/home/ryha/repos/partyboard-slots/s3/build/n121
d=$R/$1; rm -rf $d; mkdir -p $d/xdg
XDG_DATA_HOME=$d/xdg HOME=$d timeout -s KILL 1200 /home/ryha/repos/partyboard-slots/s3/build/linux-default-relwithdebinfo/partyboard \
  --script /home/ryha/repos/partyboard-slots/s3/tools/netplay/party.script --match $2@1500 --turbo --hash-log $d/hash.log > $d/out.txt 2>&1
echo "$1 exit $? frames $(wc -l < $d/hash.log)"
