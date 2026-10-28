#!/usr/bin/env bash
# N120: runs a command and samples every amdgpu GPU's busy percent (/sys/class/drm/cardN/device/gpu_busy_percent)
# once a second while it runs, then prints each GPU's sample count, mean and max to stderr. The GPUs are the
# machine's: other programs' load (the desktop, other runs) is in the numbers.
#
#   tools/netplay/gpu_busy.sh <command> [args...]
#
# Exit status: the command's.
set -euo pipefail
cards=(/sys/class/drm/card*/device/gpu_busy_percent)
[ -r "${cards[0]}" ] || { echo "gpu_busy: no gpu_busy_percent" >&2; exit 2; }
samples=$(mktemp -d)
"$@" &
pid=$!
while kill -0 "$pid" 2>/dev/null; do
    for i in "${!cards[@]}"; do
        cat "${cards[$i]}" >>"$samples/$i" 2>/dev/null || true
    done
    sleep 1
done
status=0
wait "$pid" || status=$?
for i in "${!cards[@]}"; do
    [ -s "$samples/$i" ] || continue
    awk -v f="${cards[$i]}" '{ s += $1; if ($1 > m) m = $1 } END { printf "gpu_busy: %s: %d samples, mean %.1f%%, max %d%%\n", f, NR, s / NR, m }' "$samples/$i" >&2
done
rm -rf "$samples"
exit $status
