#!/usr/bin/env bash
# N145 (from N138's): one wine run of the Windows build in a headless gamescope (no window on the desktop).
#   runwine.sh <run dir> <limit s> [game args]
# WINEPREFIX (default: prefix/ beside this script; once: gamescope --backend headless -- wineboot -i, then
# wineserver -k) and GAMEDIR (the unzipped PartyBoard folder, with wine's vulkan-1.dll copied in).
# STDIO picks how the game is started, for the GUI-subsystem exe's std handles:
#   redirect (default)  wine partyboard.exe > <run dir>/out.txt 2>&1
#   cmd                 wine cmd /c partyboard.exe (cmd's own output to out.txt; no redirect for the game)
#   start               wine start /wait /unix partyboard.exe (ShellExecute, as from Explorer)
# Each run gets a fresh AppData\Roaming\MarioPartyRD, seeded with <run dir>/config.json if there is one, and its
# logs/ copied back to <run dir>.
set -u
here=$(cd "$(dirname "$0")" && pwd)
run=$(cd "$1" && pwd) limit=$2; shift 2
export WINEPREFIX=${WINEPREFIX:-$here/prefix} WINEDLLOVERRIDES="mscoree,mshtml=" WINEDEBUG=${WINEDEBUG:--all}
appdata="$WINEPREFIX/drive_c/users/$USER/AppData/Roaming/MarioPartyRD"
rm -rf "$appdata"; mkdir -p "$appdata/Party Board"
[ -f "$run/config.json" ] && cp "$run/config.json" "$appdata/Party Board/config.json"
cd "$GAMEDIR"
case ${STDIO:-redirect} in
    redirect) cmd=(wine partyboard.exe "$@") ;;
    cmd) cmd=(wine cmd /c partyboard.exe "$@") ;;
    start) cmd=(wine start /wait /unix "$GAMEDIR/partyboard.exe" "$@") ;;
esac
timeout -s KILL "$limit" gamescope --backend headless -w 640 -h 480 -- "${cmd[@]}" > "$run/out.txt" 2>&1
echo "rc=$?"
wineserver -k 2>/dev/null
cp -r "$appdata/Party Board/logs" "$run/" 2>/dev/null
