#!/usr/bin/env bash
# The iOS "tap screen to resume controller" flag (tools/tap_resume_test.c); no game needed.
set -euo pipefail
cd "$(dirname "$0")/.."
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
cc -std=c11 -Iinclude src/port/tap_resume.c tools/tap_resume_test.c -o "$out/tap_resume_test"
"$out/tap_resume_test"
