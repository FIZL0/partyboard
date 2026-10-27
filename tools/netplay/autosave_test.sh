#!/usr/bin/env bash
# Builds src/port/netplay_autosave.c, netplay_resync.c, netplay_save.c and the file helpers of netplay_dir.cpp with
# tools/netplay/autosave_test.c (fake SLSaveBoard/SLLoadBoard and pbnet) and runs it in a temporary user dir: N47's
# per-turn online save and the local party autosave (write, offer, finish), without the game or a disc image.
set -euo pipefail
cd "$(dirname "$0")/../.."
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
defs="-DAURORA -DBYTESWAPPING -DNON_MATCHING -DTARGET_DOL -DTARGET_PC -DVERSION=1"
incs="-Iinclude -Iextern -Iextern/aurora/include -Iextern/aurora/include/dolphin -Iextern/musyx/include"
san="-g -fsanitize=address,undefined"
c++ $defs -Iinclude -std=c++20 $san -c src/port/netplay_dir.cpp -o "$out/dir.o"
cc $defs $incs -std=gnu11 $san -c src/port/netplay_autosave.c -o "$out/autosave.o"
cc $defs $incs -std=gnu11 $san -c src/port/netplay_resync.c -o "$out/resync.o"
cc $defs $incs -std=gnu11 $san -c src/port/netplay_save.c -o "$out/save.o"
cc $defs $incs -std=gnu11 $san -c tools/netplay/autosave_test.c -o "$out/test.o"
c++ -std=c++20 $san -x c++ - -c -o "$out/path.o" <<'CPP'
#include <filesystem>
extern "C" std::filesystem::path PartyBoard_ConfigPath;
std::filesystem::path PartyBoard_ConfigPath;
extern "C" void autosave_test_set_dir(const char *d) { PartyBoard_ConfigPath = d; }
CPP
c++ $san "$out"/*.o -o "$out/autosave_test"
AUTOSAVE_TEST_DIR="$out/user" "$out/autosave_test"
