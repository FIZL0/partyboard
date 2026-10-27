#!/usr/bin/env bash
# Builds src/port/netplay_match.c and the real heap code (src/game/memory.c) with tools/netplay/match_test.c
# (stub game functions) and runs it: the match start of N8, without the game or a disc image.
set -euo pipefail
cd "$(dirname "$0")/../.."
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
cc -DAURORA -DBYTESWAPPING -DNON_MATCHING -DTARGET_DOL -DTARGET_PC -DVERSION=1 \
  -Iinclude -Iextern -Iextern/aurora/include -Iextern/aurora/include/dolphin -Iextern/musyx/include \
  -std=gnu11 -g -fsanitize=address,undefined \
  src/port/netplay_match.c src/game/memory.c tools/netplay/match_test.c -o "$out/match_test"
"$out/match_test"
