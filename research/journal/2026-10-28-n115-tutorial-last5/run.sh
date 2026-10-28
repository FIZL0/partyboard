#!/bin/bash
# run.sh OUT [gdb commands file]: boot straight into overlay $OVL (default 95 = w10, the tutorial; 70 = mentDll,
# Party Mode's entry) through bootDll's PB_FCO_OVL jump (N106), all COM, tapa.script's A taps, --turbo.
# Needs a build with -DPB_FCO_BOOT:
#   cmake --preset linux-default-relwithdebinfo -B build/n115 -DCMAKE_C_FLAGS=-DPB_FCO_BOOT && cmake --build build/n115
# The game ignores SIGTERM: KILL at $LIMIT seconds (default 900). Prints the exit status and the frame count.
J=$(cd "$(dirname "$0")" && pwd); root=$(cd "$J/../../.." && pwd); out=$1
[ -f "$J/tapa.script" ] || python3 "$J/tapa.py" >"$J/tapa.script"
rm -rf "$out"; mkdir -p "$out/xdg/MarioPartyRD/Party Board"
printf '{\n    "backend.skipBootSequence": true,\n    "backend.skipPreLaunchUI": true\n}\n' \
    >"$out/xdg/MarioPartyRD/Party Board/config.json"
pre=()
[ -n "$2" ] && pre=(gdb -q -batch -x "$2" --args)
cd "$root"
env PB_FCO_OVL=${OVL:-95} PB_FCO_HUMAN=0 XDG_DATA_HOME="$out/xdg" HOME="$out" timeout -s KILL ${LIMIT:-900} \
    "${pre[@]}" "$root/${BUILD:-build/n115}/partyboard" --script "$J/tapa.script" --turbo --hash-log "$out/hash.log" \
    >"$out/out.txt" 2>&1
echo "exit $? frames $(wc -l <"$out/hash.log")"
