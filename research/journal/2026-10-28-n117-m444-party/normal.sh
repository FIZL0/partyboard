#!/usr/bin/env bash
# normal.sh <name> <script> <board>@<frame>: the normal desktop build, own user dir
root=/home/ryha/repos/partyboard-slots/s1
out=$root/build/n117/$1; rm -rf "$out"; mkdir -p "$out/xdg"
XDG_DATA_HOME="$out/xdg" HOME="$out" timeout -s KILL ${T:-3000} "$root/build/linux-default-relwithdebinfo/partyboard" \
  --script "$root/$2" --match "$3" --turbo --hash-log "$out/hash.log" >"$out/out.txt" 2>&1
echo "$1 $(wc -l <"$out/hash.log") frames, m444 $(grep -c ' ovl=52 ' "$out/hash.log"), $(grep -a 'match over' "$out/out.txt" | head -1)"
