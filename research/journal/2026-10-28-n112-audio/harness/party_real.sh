#!/usr/bin/env bash
# party_real.sh <out>: party.script --match 0@1500 with PB_AUDIO_REAL_STATUS=1 (offline's msm answers):
# run1 disk driver, run2 no audio device (a driver name SDL doesn't have)
set -u
root=/home/ryha/repos/partyboard-slots/s1; out=$1; game=$root/build/linux-default-relwithdebinfo
mkdir -p $out
run() {
    local d=$out/run$1; rm -rf $d; mkdir -p $d/xdg
    ( cd $game && env XDG_DATA_HOME=$d/xdg HOME=$d PB_AUDIO_REAL_STATUS=1 $2 timeout -s KILL ${LIMIT:-1500} ./partyboard \
        --script $root/tools/netplay/party.script --match ${MATCH:-0@1500} --turbo --hash-log $d/hash.log > $d/out.txt 2>&1 )
}
run 1 "SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=$out/run1/out.raw" &
p1=$!
run 2 "SDL_AUDIO_DRIVER=none-n112" &
p2=$!
wait $p1 $p2
for i in 1 2; do echo "run$i: $(wc -l < $out/run$i/hash.log) frames, $(grep -a -m1 'netplay: match over at frame' $out/run$i/out.txt)"; grep -a -m2 "partyboard::audio" $out/run$i/out.txt; done
$root/tools/netplay/hashdiff.py $out/run1/hash.log $out/run2/hash.log | head -5
