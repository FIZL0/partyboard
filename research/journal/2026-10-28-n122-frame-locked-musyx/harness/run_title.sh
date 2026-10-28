#!/usr/bin/env bash
# run_title.sh <name> <seconds> [env...]: offline boot to the title screen, own user dir, audio to a raw file (disk driver)
set -u
name=$1; secs=$2; shift 2
R=/home/ryha/repos/partyboard-slots/s3/build/n122/$name
rm -rf "$R"; mkdir -p "$R/xdg/MarioPartyRD/Party Board"
echo '{"backend.skipPreLaunchUI": true, "video.enableVsync": false, "backend.checkForUpdates": false}' > "$R/xdg/MarioPartyRD/Party Board/config.json"
cd /home/ryha/repos/partyboard-slots/s3/build/linux-default-relwithdebinfo
env XDG_DATA_HOME=$R/xdg HOME=$R SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=$R/out.raw "$@" timeout -s KILL $secs ./partyboard > $R/log.txt 2>&1
