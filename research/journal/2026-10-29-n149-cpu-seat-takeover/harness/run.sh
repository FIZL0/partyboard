#!/bin/sh
# N149: run.sh make|cont|mid <name> [SECS]. Needs apply.py's hook in this worktree's build. Out: .n149-runs/<name>/.
# - make: one copy hosts a new match for two local players and plays SECS (default 150) s: the per-turn online save
#   (online/m*.json in its user dir) is the one cont and mid continue.
# - cont SAVE: copy 1 hosts the continue of SAVE (Hosty alone), copy 2 joins over LAN as Carl, takes seat 4 (a CPU
#   seat) and readies; the host starts; both mash A for SECS (default 200) with --hash-log.
# - mid SAVE: copy 1 starts the continue of SAVE alone (Pal's seat 2 is the computer's), copy 2 joins over LAN once it
#   runs and takes Pal's seat at the next resync point; both mash A for SECS.
cd "$(dirname "$0")/../../../.."
MODE=$1
R=$PWD/.n149-runs/$2
case $MODE in make) SECS=${3:-150} ;; *) SAVE=$3; SECS=${4:-200} ;; esac
rm -rf "$R"
mkdir -p "$R/x"
for p in host guest; do
  mkdir -p "$R/$p/data/MarioPartyRD/Party Board"
  printf '{"video.enableVsync": false, "backend.skipPreLaunchUI": true}\n' > "$R/$p/data/MarioPartyRD/Party Board/config.json"
done
cd build/linux-default-relwithdebinfo
export WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-1}
if [ "$MODE" = make ]; then
  env XDG_DATA_HOME="$R/host/data" HOME="$R/host" PB_N149="$R/x" PB_N149_MODE=make timeout -s KILL $((SECS + 30)) \
    ./partyboard --hash-log "$R/host.hash" > "$R/host.out" 2>&1 < /dev/null &
  HOST=$!
  sleep "$SECS"
  kill -9 $HOST 2>/dev/null
  wait $HOST 2>/dev/null
  find "$R/host/data" -path '*online/*.json' -exec ls -l {} \;
  grep -h "n149:\|netplay: \(desync\|lobby started\|resync point\)" "$R/host.out" | cut -c1-300 | tail -20
  exit 0
fi
MID=
[ "$MODE" = mid ] && MID=1
env XDG_DATA_HOME="$R/host/data" HOME="$R/host" PB_N149="$R/x" PB_N149_MODE=host PB_N149_SAVE="$SAVE" ${MID:+PB_N149_MID=1} \
  timeout -s KILL $((SECS + 90)) ./partyboard --hash-log "$R/host.hash" > "$R/host.out" 2>&1 < /dev/null &
HOST=$!
env XDG_DATA_HOME="$R/guest/data" HOME="$R/guest" PB_N149="$R/x" PB_N149_MODE=guest ${MID:+PB_N149_MID=1} \
  timeout -s KILL $((SECS + 90)) ./partyboard --hash-log "$R/guest.hash" > "$R/guest.out" 2>&1 < /dev/null &
GUEST=$!
if [ -z "$MID" ]; then
  for i in $(seq 1 90); do [ -e "$R/x/guestwait.txt" ] && break; sleep 1; done
  sleep 1
  echo 1 > "$R/x/start.txt"
fi
sleep "$SECS"
kill -9 $HOST $GUEST 2>/dev/null
wait $HOST $GUEST 2>/dev/null
grep -h "n149:\|pbnet: \(reload\|.*joined\|.*takes\)\|netplay: \(desync\|lobby started\|resync point\|reload\)" "$R/host.out" | sed 's/^/host  /' | cut -c1-400
grep -h "n149:\|pbnet: reload\|netplay: \(desync\|lobby started\|resync point\|reload\)" "$R/guest.out" | sed 's/^/guest /' | cut -c1-400
