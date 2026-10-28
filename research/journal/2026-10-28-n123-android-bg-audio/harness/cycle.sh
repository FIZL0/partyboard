#!/bin/bash
# N123: background/foreground cycles on a running emulator; prints the game's AAudio player state after each step.
# usage: cycle.sh <outdir> [cycles]   (game already running in the foreground)
set -u
OUT=$1; N=${2:-3}; P=com.mariopartyrd.partyboard
export PATH=$HOME/Android/Sdk/platform-tools:$PATH
mkdir -p "$OUT"
state() { adb shell dumpsys audio | grep 'type:AAudio' | grep -o 'state:[a-z]*' | tr '\n' ' '; }
fg() { adb shell am start -n $P/.PartyBoardActivity >/dev/null 2>&1; }
step() { # name, action
  eval "$2"; sleep 6
  echo "$1: $(state) pid=$(adb shell pidof $P | tr -d '\r')"
}
for i in $(seq 1 "$N"); do
  step "c$i home" "adb shell input keyevent KEYCODE_HOME"
  step "c$i fg" fg
  adb exec-out screencap -p > "$OUT/c$i-fg-a.png"; sleep 1; adb exec-out screencap -p > "$OUT/c$i-fg-b.png"
  step "c$i power-off" "adb shell input keyevent KEYCODE_SLEEP"
  step "c$i power-on" "adb shell input keyevent KEYCODE_WAKEUP; adb shell wm dismiss-keyguard"
  step "c$i recents" "adb shell input keyevent KEYCODE_APP_SWITCH"
  step "c$i back-from-recents" fg
done
adb logcat -d > "$OUT/logcat.txt"
grep -E 'timeout|ANR in|FATAL|Fatal signal' "$OUT/logcat.txt" | grep -i -E 'partyboard|timeout|ANR|Fatal' | head
echo "paused: $(grep -c 'Paused for the background' "$OUT/logcat.txt") resumed: $(grep -c 'Resumed in the foreground' "$OUT/logcat.txt")"
