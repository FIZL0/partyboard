# usage: source drive.sh ; helpers for the emulator
# Taps in landscape 2400x1080 device pixels (AVD hst34); tap3.sh must be pushed to /data/local/tmp first.
A=~/Android/Sdk/platform-tools/adb
S=${S:-.}   # where screenshots go
P=com.mariopartyrd.partyboard
tap() { $A shell input tap "$1" "$2"; sleep "${3:-1.5}"; }
shot() { $A exec-out screencap -p > "$S/$1.png"; }
tap3() { $A shell sh /data/local/tmp/tap3.sh; sleep 2; }
launch() { $A shell am force-stop $P; $A shell monkey -p $P -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1; sleep 12; }
to_join() { launch; tap 2160 366 15; tap3; tap 698 84 2; tap 492 151 2; }
