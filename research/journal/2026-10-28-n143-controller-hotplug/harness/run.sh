#!/usr/bin/env bash
# N143: one desktop run with PB_LIFECYCLE_TEST pad steps (src/port/lifecycle.cpp). run.sh <name> <steps> [args...]
# User dir and out.log in .n143-runs/<name>/ (gitignored); SECS= run length (default 40), PB_LIFECYCLE_SHOT= for uishot.
set -u
cd "$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
name=$1; steps=$2; shift 2
d=$PWD/.n143-runs/$name
rm -rf "$d"; mkdir -p "$d/data" "$d/home"
[ -f "$PWD/.n143-runs/config.json" ] && mkdir -p "$d/data/MarioPartyRD/Party Board" && cp "$PWD/.n143-runs/config.json" "$d/data/MarioPartyRD/Party Board/"
WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-1} XDG_DATA_HOME=$d/data HOME=$d/home PB_HOME_MUSIC=0 \
  PB_LIFECYCLE_TEST="$steps" timeout -s KILL ${SECS:-40} build/linux-default-relwithdebinfo/partyboard "$@" >"$d/out.log" 2>&1
echo "exit $?"
