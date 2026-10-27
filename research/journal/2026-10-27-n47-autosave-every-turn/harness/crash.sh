#!/bin/bash
# crash.sh <name> <min frame> -- args : runs, SIGKILLs once the online save's frame >= min frame
name=$1; minf=$2; shift 3
S=/tmp/claude-1000/-home-ryha-repos-partyboard-slots-s2/52678b62-248e-4ef0-9afb-aa1d1d10a0ba/scratchpad
R=/home/ryha/repos/partyboard-slots/s2/.n47-runs
$S/run.sh $name 120 -- "$@" &
f="$R/$name/MarioPartyRD/Party Board/online/local-0000000050424E54.json"
for i in $(seq 1 12000); do
  if [ -f "$f" ]; then
    fr=$(python3 -c "import json,sys; print(json.load(open(sys.argv[1]))['frame'])" "$f" 2>/dev/null || echo 0)
    if [ "$fr" -ge "$minf" ]; then for p in $(pgrep -x partyboard); do [ "$(readlink /proc/$p/cwd)" = "$R/$name" ] && kill -KILL $p; done; echo "killed at save frame $fr"; break; fi
  fi
  sleep 0.01
done
wait
