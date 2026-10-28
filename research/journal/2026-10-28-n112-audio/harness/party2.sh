#!/usr/bin/env bash
# party2.sh <out> <game dir>: party_check.sh's two --match 0@1500 party runs, run1 audio to a raw file (disk driver),
# run2 SDL's dummy driver (other audio timing), then hashdiff.py
set -u
root=/home/ryha/repos/partyboard-slots/s1
out=$1; game=${2:-$root/build/linux-default-relwithdebinfo}; script=${SCRIPT:-$root/tools/netplay/party.script}
match=${MATCH:-0@1500}
mkdir -p $out
run() {
    local d=$out/run$1; rm -rf $d; mkdir -p $d/xdg
    ( cd $game && env XDG_DATA_HOME=$d/xdg HOME=$d $2 timeout -s KILL ${LIMIT:-1500} ./partyboard --script $script \
        --match $match --turbo --hash-log $d/hash.log > $d/out.txt 2>&1 )
}
run 1 "SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=$out/run1/out.raw" &
p1=$!
run 2 "SDL_AUDIO_DRIVER=dummy" &
p2=$!
wait $p1 $p2
for i in 1 2; do echo "run$i: $(wc -l < $out/run$i/hash.log) frames, $(grep -a -m1 'netplay: match over at frame' $out/run$i/out.txt)"; done
$root/tools/netplay/hashdiff.py $out/run1/hash.log $out/run2/hash.log
