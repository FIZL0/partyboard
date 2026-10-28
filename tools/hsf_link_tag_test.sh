#!/usr/bin/env bash
# N81: links tools/hsf_link_tag_test.c against the built libdol.so (Hu3DModelLink / Hu3DModelKill and the heap; the
# rest of the game is left unresolved and never called) and runs it: a link whose slot held an earlier link must not
# lose its HSFDATA to the kill of another link now at that address. Needs the desktop build (tools/check.sh's last step).
set -euo pipefail
cd "$(dirname "$0")/.."
lib=$PWD/build/${1:-linux-default-relwithdebinfo}
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
cc -DBYTESWAPPING -DNON_MATCHING -DTARGET_DOL -DTARGET_PC -DVERSION=1 \
  -Iinclude -Iextern -Iextern/aurora/include -Iextern/aurora/include/dolphin \
  -std=gnu11 -g tools/hsf_link_tag_test.c -o "$out/hsf_link_tag_test" \
  -L"$lib" -ldol -Wl,-rpath,"$lib:$lib/_deps/fmt-build" -Wl,--unresolved-symbols=ignore-all
"$out/hsf_link_tag_test" | grep -v '^HuMem>'
