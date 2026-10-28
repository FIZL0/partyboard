#!/usr/bin/env bash
# base_party.sh <out>: party.script --match 0@1500 on the pre-N112 build (merge base 37919cf5, build/n112/base-src)
d=$1; rm -rf $d; mkdir -p $d/xdg
cd /home/ryha/repos/partyboard-slots/s1/build/n112/base-src/build/linux-default-relwithdebinfo
XDG_DATA_HOME=$d/xdg HOME=$d timeout -s KILL ${LIMIT:-2400} ./partyboard --script /home/ryha/repos/partyboard-slots/s1/tools/netplay/party.script \
    --match 0@1500 --turbo --hash-log $d/hash.log > $d/out.txt 2>&1
echo "base: $(wc -l < $d/hash.log) frames, $(grep -a -m1 'netplay: match over at frame' $d/out.txt)"
