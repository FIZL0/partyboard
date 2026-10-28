#!/usr/bin/env bash
# Test gate for agent-runner: net crate tests, then the desktop build.
set -euo pipefail
cd "$(dirname "$0")/.."
if [ -f net/Cargo.toml ]; then
  cargo test --quiet --manifest-path net/Cargo.toml
  # N92: the browser's transport (WebRTC, net/src/rtc.rs) over a fake page, without iroh
  cargo test --quiet --manifest-path net/Cargo.toml --no-default-features
fi
# worktrees (slots, merge) start without submodules
[ -f extern/aurora/CMakeLists.txt ] || git submodule update --init --recursive --quiet
# netplay lockstep gate and pad injection (C, fake pbnet; no game needed)
[ ! -x tools/netplay/tick_gate_test.sh ] || tools/netplay/tick_gate_test.sh >/dev/null
# netplay match start (C, stub game functions, real heap code; no game needed)
[ ! -x tools/netplay/match_test.sh ] || tools/netplay/match_test.sh >/dev/null
# N47 per-turn saves and the local party autosave (C, fake pbnet, real file helpers; no game needed)
[ ! -x tools/netplay/autosave_test.sh ] || tools/netplay/autosave_test.sh >/dev/null
preset=linux-default-relwithdebinfo
[ -f "build/$preset/build.ninja" ] || cmake --preset "$preset" >/dev/null
out=$(cmake --build --preset "$preset" 2>&1) || { echo "$out" | grep -E -A5 "error|FAILED" | head -60; exit 1; }
# N81 link tags (C against the built libdol.so; no game needed)
[ ! -x tools/hsf_link_tag_test.sh ] || tools/hsf_link_tag_test.sh "$preset" >/dev/null
