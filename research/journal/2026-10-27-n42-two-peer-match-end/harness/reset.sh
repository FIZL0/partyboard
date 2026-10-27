#!/bin/sh
# reset.sh: both peers' user dirs back to: the turn-10 save on the host, no rejoin record, no desync logs
P="Party Board"
mkdir -p old host/data/MarioPartyRD/"$P"/online
mv host/data/MarioPartyRD/"$P"/desync-* guest/data/MarioPartyRD/"$P"/desync-* old/ 2>/dev/null
rm -f guest/data/MarioPartyRD/"$P"/rejoin.txt host/data/MarioPartyRD/"$P"/rejoin.txt guest/data/MarioPartyRD/"$P"/online/* host/data/MarioPartyRD/"$P"/online/*
cp t10.json host/data/MarioPartyRD/"$P"/online/local-0000000050424E54.json
rm -f x/code
