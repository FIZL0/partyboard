#!/usr/bin/env bash
# Test gate for agent-runner: net crate tests, then the desktop build.
set -euo pipefail
cd "$(dirname "$0")/.."
if [ -f net/Cargo.toml ]; then
  cargo test --quiet --manifest-path net/Cargo.toml
fi
# worktrees (slots, merge) start without submodules
[ -f extern/aurora/CMakeLists.txt ] || git submodule update --init --recursive --quiet
preset=linux-default-relwithdebinfo
[ -f "build/$preset/build.ninja" ] || cmake --preset "$preset" >/dev/null
out=$(cmake --build --preset "$preset" 2>&1) || { echo "$out" | grep -E -A5 "error|FAILED" | head -60; exit 1; }
