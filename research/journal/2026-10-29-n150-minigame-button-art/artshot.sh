#!/bin/bash
# N150: sheet of every known button picture: original, then redrawn as PlayStation, Xbox, Switch, Generic, Touch.
# artshot.sh <raw dir (unpack output)> <out.png>   (run from the repo root, after a linux-default build)
set -e
J=$(dirname "$0"); RAW=$1; OUT=$2; B=$PWD/build/linux-default-relwithdebinfo
T=$(mktemp -d)
cc -O1 -o $T/artshot $J/artshot.c -L$B -ldol -Wl,-rpath,$B -Wl,--unresolved-symbols=ignore-all
LIST="m405/068:0 m405/069:0 m405/070:0 m405/071:0 m432/037:0 m432/038:0 m432/039:0 mgmode/024:0 mgmode/046:0
mgmode/047:0 mpex/030:0 gamemes/005:0 option/049:0 m418/018:0 m418/019:0 m418/020:0 m418/021:0 m460/009:0 m460/010:0
inst/000:10 inst/000:14"
for it in $LIST; do
    for fam in 1 2 3 4 6; do
        $T/artshot $RAW/${it%:*}.dat ${it#*:} $fam $T/$(echo $it | tr '/:' '__')-$fam.rgba
    done
done
python3 -I $J/artsheet.py $RAW $T $OUT $LIST
rm -rf $T
