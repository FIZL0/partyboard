#!/bin/sh
# N140: party.script --match 0@1500 --turbo --hash-log in headless Chromium (tools/web_check.mjs), SECS seconds, the
# hash log copied out to OUT/hash.log. Usage: party.sh WEB_ROOT OUT SECS [extra query, e.g. '&disc=memory']
cd "$(dirname "$0")/../../.."
WEB_ROOT=$1 SHOT_EVERY=60 STOP_ON=RuntimeError WEB_FILES=tools/netplay/party.script DUMP_FILES=/tmp/hash.log \
SDL_APP_ID=partyboard-test exec timeout -s KILL $(( $3 + 300 )) node tools/web_check.mjs "$PB_DISC" "$2" "$3" \
    "args=--script /test/party.script --match 0@1500 --turbo --hash-log /tmp/hash.log&fetch=party.script$4"
