#!/usr/bin/env bash
# grav.sh <name> <ovl> <human mask> <script> [grav]: boot jump on build/n117-grav, PB_N117_GRAV set when grav=1
root=/home/ryha/repos/partyboard-slots/s1
out=$root/build/n117/$1; rm -rf "$out"; mkdir -p "$out/xdg/MarioPartyRD/Party Board"
printf '{\n    "backend.skipBootSequence": true,\n    "backend.skipPreLaunchUI": true\n}\n' >"$out/xdg/MarioPartyRD/Party Board/config.json"
g=(); [ "${5:-0}" = 1 ] && g=(PB_N117_GRAV=1)
env "${g[@]}" PB_FCO_OVL=$2 PB_FCO_HUMAN=$3 XDG_DATA_HOME="$out/xdg" HOME="$out" timeout -s KILL ${T:-900} nice -n 19 \
  "$root/build/n117-grav/partyboard" --script "$root/$4" --turbo --hash-log "$out/hash.log" >"$out/out.txt" 2>&1
echo "$1 $(wc -l <"$out/hash.log") frames, m444 $(grep -c ' ovl=52 ' "$out/hash.log")"
