#!/usr/bin/env bash
# Builds src/port/netplay_match.c and the real heap code (src/game/memory.c) with tools/netplay/match_test.c
# (stub game functions) and runs it: the match start of N8, the continue and checkpoint of N9 (with
# src/port/netplay_save.c), without the game or a disc image. The room JSON the checkpoint writes for a match with no
# lobby is then read by pbnet's own Room parser (save.rs test c_room_json_is_a_room, via MATCH_TEST_ROOM).
set -euo pipefail
cd "$(dirname "$0")/../.."
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
cc -DAURORA -DBYTESWAPPING -DNON_MATCHING -DTARGET_DOL -DTARGET_PC -DVERSION=1 \
  -Iinclude -Iextern -Iextern/aurora/include -Iextern/aurora/include/dolphin -Iextern/musyx/include \
  -std=gnu11 -g -fsanitize=address,undefined \
  src/port/netplay_match.c src/port/netplay_save.c src/game/memory.c tools/netplay/match_test.c -o "$out/match_test"
MATCH_TEST_ROOM="$out/room.json" "$out/match_test"
if [ -f net/Cargo.toml ]; then
  MATCH_TEST_ROOM="$out/room.json" cargo test --quiet --manifest-path net/Cargo.toml --lib save::tests::c_room_json_is_a_room
fi
