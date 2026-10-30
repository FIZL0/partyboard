#!/bin/bash
# N150: one shot run. run.sh <out dir> <family: gamecube|xbox|playstation|keyboard|...> <overlay index> [inst] [script]
# Boots straight into the overlay (bootDll's PB_FCO_OVL jump: build/n150-shot = the desktop preset with
# -DPB_FCO_BOOT), seat 0 human on pad 0 (HUMAN=<mask> for others), PB_ICON_FAMILY forced, PB_ART_SHOT captures each known picture's first draw.
# "inst" shows the minigame's instructions first. SCRIPT (default: idle, end 2400) drives the pads.
set -e
OUT=$1; FAM=$2; OVL=$3; INST=$4; SCRIPT=${5:-}
ROOT=$PWD
export WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-1} # the agent shell has none (N132)
mkdir -p "$OUT/xdg/MarioPartyRD/Party Board" "$OUT/shots"
OUT=$(cd "$OUT" && pwd)
printf '{\n    "backend.skipBootSequence": true,\n    "backend.skipPreLaunchUI": true\n}\n' >"$OUT/xdg/MarioPartyRD/Party Board/config.json"
if [ -z "$SCRIPT" ]; then
    SCRIPT=$OUT/idle.script
    printf 'end 2400\n' >"$SCRIPT"
fi
env ${KEYS:+PB_N150_KEYS=1} PB_FCO_OVL=$OVL PB_FCO_HUMAN=${HUMAN:-1} ${INST:+PB_FCO_INST=1} PB_ICON_FAMILY=$FAM PB_ICON_LOG=1 PB_ART_SHOT="$OUT/shots" \
    PB_ART_SHOT_DELAYS=${DELAYS:-20,120,300} XDG_DATA_HOME="$OUT/xdg" HOME="$OUT" \
    timeout -s KILL ${LIMIT:-150} "$ROOT/build/n150-shot/partyboard" --script "$SCRIPT" --turbo --hash-log "$OUT/hash.log" \
    >"$OUT/out.txt" 2>&1 || true
echo "$OUT: $(wc -l <"$OUT/hash.log" 2>/dev/null) frames, $(ls "$OUT/shots" | wc -l) shots"
grep -a "button_art:\|button_icons: ovl" "$OUT/out.txt" | sort -u | head -20
