#!/usr/bin/env bash
# N138: one wine run of the Windows build in a headless gamescope (no window on the desktop).
#   runwine.sh <run dir> <limit s> [game args]
# Expects, beside this script: prefix/ (once: WINEPREFIX=$PWD/prefix gamescope --backend headless -- wineboot -i,
# then wineserver -k) and wine1/PartyBoard/ (the unzipped package), or GAMEDIR=<dir holding partyboard.exe>.
# One prefix for all runs; each run gets a fresh AppData\Roaming\MarioPartyRD, seeded with <run dir>/config.json
# if there is one, and its logs/ copied back to <run dir>. The game's stdout/stderr go to <run dir>/out.txt.
# Under wine, Vulkan needs wine's vulkan-1.dll copied beside partyboard.exe (Dawn's system32 load fails, error 87)
# and {"backend.graphicsBackend": "vulkan"} in config.json (wine's D3D12 has no shared fences).
set -u
here=$(cd "$(dirname "$0")" && pwd)
run=$1 limit=$2; shift 2
export WINEPREFIX=$here/prefix WINEDLLOVERRIDES="mscoree,mshtml=" WINEDEBUG=${WINEDEBUG:--all}
appdata="$WINEPREFIX/drive_c/users/$USER/AppData/Roaming/MarioPartyRD"
rm -rf "$appdata"; mkdir -p "$appdata/Party Board"
[ -f "$run/config.json" ] && cp "$run/config.json" "$appdata/Party Board/config.json"
cd "${GAMEDIR:-$here/wine1/PartyBoard}"
timeout -s KILL "$limit" gamescope --backend headless -w 640 -h 480 -- wine partyboard.exe "$@" > "$run/out.txt" 2>&1
echo "rc=$?"
wineserver -k 2>/dev/null
cp -r "$appdata/Party Board/logs" "$run/" 2>/dev/null
