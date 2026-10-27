#!/usr/bin/env bash
# Builds src/port/netplay.c with tools/netplay/tick_gate_test.c (fake pbnet, fake PADRead, stub game state)
# and runs it: the lockstep gate and pad injection of N5, without the game or a disc image.
set -euo pipefail
cd "$(dirname "$0")/../.."
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
cc -DAURORA -DBYTESWAPPING -DNON_MATCHING -DTARGET_DOL -DTARGET_PC -DVERSION=1 \
  -Iinclude -Iextern -Iextern/aurora/include -Iextern/aurora/include/dolphin -Iextern/musyx/include \
  -std=gnu11 -g -fsanitize=address,undefined \
  src/port/netplay.c tools/netplay/tick_gate_test.c -o "$out/tick_gate_test"
"$out/tick_gate_test"
