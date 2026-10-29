#!/bin/sh
# N140: runs tools/web_check.mjs (args after OUT as for it) and samples the --type=renderer processes under it every
# second: OUT/hwm.txt gets each one's VmHWM/VmRSS (kB), the last line their peak. Only walks its own child tree.
# Usage: hwm.sh OUT SECS [QUERY]   env as web_check.mjs (WEB_ROOT, ...); DISC (default $PB_DISC), HWM_EVERY (1 s)
set -e
out=$1; secs=$2; query=$3
cd "$(dirname "$0")/../../.."
mkdir -p "$out"
SDL_APP_ID=partyboard-test node tools/web_check.mjs "${DISC:-$PB_DISC}" "$out" "$secs" "$query" > "$out/web_check.log" 2>&1 &
root=$!
desc() { for c in $(cat /proc/$1/task/*/children 2>/dev/null); do echo $c; desc $c; done; }
peak=0
: > "$out/hwm.txt"
while kill -0 $root 2>/dev/null; do
    for p in $(desc $root); do
        tr '\0' ' ' < /proc/$p/cmdline 2>/dev/null | grep -q -- '--type=renderer' || continue
        h=$(awk '/VmHWM/{print $2}' /proc/$p/status 2>/dev/null)
        [ -n "$h" ] || continue
        echo "$(date +%s) $p hwm=$h $(awk '/^(VmRSS|RssAnon|RssFile|RssShmem|VmSwap)/{printf "%s%s ", $1, $2}' /proc/$p/status)" >> "$out/hwm.txt"
        [ "$h" -gt "$peak" ] && peak=$h
    done
    sleep ${HWM_EVERY:-1}
done
wait $root || true
echo "peak renderer VmHWM ${peak} kB" | tee -a "$out/hwm.txt"
