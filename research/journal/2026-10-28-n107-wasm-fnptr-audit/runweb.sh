#!/bin/sh
# runweb.sh ROOT OUT BOARD PORTBASE SECS: one web party on board BOARD (0..5) with party.script
cd /home/ryha/repos/partyboard-slots/s2
WEB_ROOT=$1 WEB_PORT=$(( $4 )) CDP_PORT=$(( $4 + 1 )) SHOT_EVERY=300 STOP_ON=RuntimeError \
WEB_FILES=tools/netplay/party.script DUMP_FILES=/tmp/hash.log \
exec timeout -s KILL $(( $5 + 300 )) node tools/web_check.mjs "$PB_DISC" "$2" "$5" "args=--script /test/party.script --match $3@1500 --turbo --hash-log /tmp/hash.log&fetch=party.script"
