#!/usr/bin/env bash
# live_volume.sh: offline boot to the title screen under gdb, disk audio, volume changed live (${GDB_SCRIPT:-live_volume.gdb})
set -u
H=$(cd "$(dirname "$0")" && pwd)
R=/home/ryha/repos/partyboard-slots/s1/build/n124/${RUN:-live}
rm -rf "$R"; mkdir -p "$R/xdg/MarioPartyRD/Party Board"
echo '{"backend.skipPreLaunchUI": true, "video.enableVsync": false, "backend.checkForUpdates": false, "audio.masterVolume": 100}' > "$R/xdg/MarioPartyRD/Party Board/config.json"
cd /home/ryha/repos/partyboard-slots/s1/build/linux-default-relwithdebinfo
env XDG_DATA_HOME=$R/xdg HOME=$R SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=$R/out.raw \
    timeout -s KILL 240 gdb -batch -x "$H/${GDB_SCRIPT:-live_volume.gdb}" ./partyboard > $R/log.txt 2>&1
python3 /home/ryha/repos/partyboard-slots/s1/research/journal/2026-10-28-n112-audio/harness/rms.py $R/out.raw
