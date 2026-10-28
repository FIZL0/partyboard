#!/usr/bin/env bash
# N131: one real-time run (vsync off, 60 Hz frame limiter) of a script; prints each "n131 mark ovl" line (needs
# harness/trace.diff applied) with the seconds since launch. Usage: timed_run.sh <name> <script>
set -euo pipefail
cd "$(dirname "$0")/../../../.."
d=build/n131/$1
rm -rf "$d"; mkdir -p "$d/xdg/MarioPartyRD/Party Board"
echo '{"video.enableVsync": false}' > "$d/xdg/MarioPartyRD/Party Board/config.json"
WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-1} XDG_DATA_HOME=$PWD/$d/xdg HOME=$PWD/$d timeout -s KILL 150 \
    build/linux-default-relwithdebinfo/partyboard --script "$2" 2>&1 | python3 -u -c '
import sys, time
t0 = time.monotonic()
for line in sys.stdin.buffer:
    s = line.decode("utf-8", "replace")
    if "n131 mark ovl" in s:
        print(f"{time.monotonic() - t0:7.2f}s {s.strip()}", flush=True)
' | tee "$d/marks.txt" || true
