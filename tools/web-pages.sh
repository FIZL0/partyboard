#!/bin/sh
# tools/web-pages.sh — puts the browser build (build/web/, from tools/web.sh) on GitHub Pages: https://fizl0.github.io/partyboard/
# The gh-pages branch is one orphan commit, force-pushed, so old wasm builds don't pile up in the repo.
# Pages can't send the precompressed .br/.gz with Content-Encoding (tools/web.sh, N90), so they stay out.
set -e
cd "$(dirname "$0")/.."
[ -f build/web/index.html ] || { echo "no build/web: run tools/web.sh first" >&2; exit 1; }
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
rsync -a --exclude '*.br' --exclude '*.gz' build/web/ "$tmp/"
touch "$tmp/.nojekyll"
git -C "$tmp" init -q -b gh-pages
git -C "$tmp" add -A
git -C "$tmp" commit -q -m "Web build $(git describe --tags --match 'v*' 2>/dev/null || git rev-parse --short HEAD)"
git -C "$tmp" push -qf "$(git remote get-url origin)" gh-pages
echo "pushed gh-pages; live at https://fizl0.github.io/partyboard/ in a minute or two"
