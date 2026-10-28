#!/bin/sh
# N87: builds the browser version into build/web/: the page (platforms/web), partyboard.js/.wasm and partyboard.data
# (res/ only; no game data: the player picks their disc in the page). Serve build/web/ over http on localhost, or
# https elsewhere (OPFS, where the page keeps the disc, needs a secure context); no COOP/COEP needed (no threads).
#
# Needs emsdk (EMSDK, default ~/emsdk; `./emsdk install latest && ./emsdk activate latest`) and rustup's
# wasm32-unknown-emscripten target (aurora's nod). WEB_FAST=1 skips the final wasm-opt and the compression. WEB_DEBUG=1
# builds with Emscripten's runtime assertions and names (build/web-cmake is reconfigured either way).
#
# N90: a release build also writes partyboard.{wasm,data,js}.br and .gz (brotli -q 11, gzip -9; brotli only if the
# tool is there) for a server that serves precompressed files (nginx gzip_static/brotli_static, Caddy's
# `file_server { precompressed br gzip }`), with Content-Encoding and the original Content-Type. tools/web_check.mjs
# serves them so.
set -e
cd "$(dirname "$0")/.."
EMSDK=${EMSDK:-$HOME/emsdk}
if [ ! -f "$EMSDK/emsdk_env.sh" ]; then
    echo "web.sh: no emsdk at $EMSDK (set EMSDK)" >&2
    exit 1
fi
. "$EMSDK/emsdk_env.sh" >/dev/null 2>&1
export EMSDK
if ! rustup target list --installed 2>/dev/null | grep -qx wasm32-unknown-emscripten; then
    echo "web.sh: rustup target add wasm32-unknown-emscripten" >&2
    exit 1
fi
cmake --preset web -DPB_WEB_DEBUG=$([ -n "$WEB_DEBUG" ] && echo ON || echo OFF) >/dev/null
cmake --build --preset web

out=build/web
rm -rf "$out"
mkdir -p "$out"
cp build/web-cmake/partyboard.js build/web-cmake/partyboard.data "$out/"
if [ -n "$WEB_FAST" ] || [ -n "$WEB_DEBUG" ]; then
    cp build/web-cmake/partyboard.wasm "$out/"
else
    # The link stops at -O1 so nothing is inlined before Asyncify runs (cmake/web.cmake); optimize after it.
    "$EMSDK/upstream/bin/wasm-opt" -O2 --enable-exception-handling --enable-bulk-memory --enable-bulk-memory-opt \
        --enable-nontrapping-float-to-int --enable-sign-ext --enable-mutable-globals --enable-multivalue \
        --enable-reference-types --enable-call-indirect-overlong \
        build/web-cmake/partyboard.wasm -o "$out/partyboard.wasm"
fi
cp platforms/web/index.html platforms/web/pb.js "$out/"
if [ -z "$WEB_FAST" ] && [ -z "$WEB_DEBUG" ]; then
    for f in partyboard.wasm partyboard.data partyboard.js; do
        gzip -9 -k -n "$out/$f" &
        if command -v brotli >/dev/null 2>&1; then
            brotli -q 11 -k "$out/$f" &
        fi
    done
    wait
fi
cp res/icon.png res/logo.png "$out/"
git rev-parse --short=7 HEAD 2>/dev/null > "$out/version.txt" || date +%s > "$out/version.txt"
ls -l "$out"
