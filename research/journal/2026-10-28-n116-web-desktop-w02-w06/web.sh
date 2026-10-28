#!/bin/sh
# web.sh NAME BOARD PORTBASE SECS: one browser party (party.script with no end, so the page still answers CDP) on BOARD
cd /home/ryha/repos/partyboard-slots/s3
R=build/n116
WEB_ROOT=$R/${ROOT:-web} WEB_PORT=$3 CDP_PORT=$(( $3 + 1 )) SHOT_EVERY=300 STOP_ON=RuntimeError \
WEB_FILES=$R/party-noend.script DUMP_FILES=/tmp/hash.log \
timeout -s KILL $(( $4 + 300 )) node tools/web_check.mjs "$PB_DISC" $R/$1 $4 "args=--script /test/party-noend.script --match $2@1500 --turbo --hash-log /tmp/hash.log&fetch=party-noend.script"
echo "$1 exit $?"
