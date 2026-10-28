#!/bin/sh
# case.sh <outroot> <name> <board> <mode> <EVENT> <DELAY> [force commands...]: one N66 reload.sh run.
# EVENT may hold a gdb condition, spaces written as ~. The force
# commands are N66's force-*.sh or this dir's (each "script args", ';'-separated), concatenated into one FORCE file.
# usage via cases.txt: xargs -P4 -L1 case.sh <outroot> < cases.txt
H=$(cd "$(dirname "$0")" && pwd)
N66=$(cd "$H/../../2026-10-27-n66-board-reload-statics/harness" && pwd)
ROOT=$1 NAME=$2 BOARD=$3 MODE=$4 EVENT=$(echo "$5" | tr "~" " ") DELAY=$6
shift 6
OUT=$ROOT/$NAME
mkdir -p "$OUT"
: > "$OUT/force.gdb"
echo "$*" | tr ';' '\n' | while read -r f args; do
  [ -z "$f" ] && continue
  if [ -f "$H/$f" ]; then sh "$H/$f" $args; else sh "$N66/$f" $args; fi >> "$OUT/force.gdb"
done
BOARD=$BOARD FORCE=$OUT/force.gdb TMO=${TMO:-400} sh "$N66/reload.sh" "$OUT" "$EVENT" "$DELAY" "$MODE"
echo "case $NAME done"
