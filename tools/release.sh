#!/usr/bin/env bash
# tools/release.sh VERSION [notes.md] [publish] — a GitHub release vVERSION ("Party Board VERSION") of the unsigned
# iOS IPA (tools/ios-ipa.sh) and its sideloading source (tools/ios-source.sh), as opentennis's tools/release.sh.
# A draft unless `publish` is given; VERSION with a suffix (1.0.0-rc.1) is a pre-release, never Latest. notes.md is the
# "New in …" list; without it, the commit subjects since the last v* tag. A published (Latest) release also rebuilds
# the shared FIZL0 source (github.com/FIZL0/apps) at once instead of within the hour.
# ponytail: iOS only; add the Linux/Windows/APK builds here when the fork ships them.
set -euo pipefail
cd "$(dirname "$0")/.."
repo=FIZL0/partyboard source=https://fizl0.github.io/apps/source.json
notes= publish=--draft version=
for a; do
    case $a in
        publish) publish=--latest ;;
        v[0-9]*|[0-9]*) version=${a#v} ;;
        *) notes=$a ;;
    esac
done
[[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.]+)?$ ]] || { echo "usage: tools/release.sh VERSION [notes.md] [publish]" >&2; exit 1; }
tag=v$version pre=
[[ $version == *-* ]] && { pre=--prerelease; [ "$publish" = --draft ] || publish=--latest=false; }
[ -z "$notes" ] || [ -f "$notes" ] || { echo "no such notes file: $notes" >&2; exit 1; }
last=$(git describe --tags --abbrev=0 --match 'v[0-9]*' 2>/dev/null || true)
[ -z "$(git status --porcelain --untracked-files=no --ignore-submodules)" ] || { echo "commit first: the release is tagged at HEAD" >&2; exit 1; }
! gh release list -R $repo -L 100 --json tagName -q '.[].tagName' | grep -qx "$tag" || { echo "$tag already has a release (a draft?)" >&2; exit 1; }

out=build/release
rm -rf "$out" && mkdir -p "$out"
tools/ios-ipa.sh "$out/partyboard-ios.ipa"
tools/ios-source.sh "$version" "$out/partyboard-ios.ipa" >/dev/null
body=$out/notes.md
{
    [ -z "$pre" ] || { echo "> **Pre-release:** a test build of the next version."; echo; }
    echo "Party Board is a PC/mobile port of the Mario Party 4 decompilation, with online play."
    echo
    echo "## Download"
    echo
    echo "| Platform | File | How to play |"
    echo "|---|---|---|"
    echo "| **iPhone** / **iPad** (iOS 15+) | [\`partyboard-ios.ipa\`](https://github.com/$repo/releases/download/$tag/partyboard-ios.ipa) | Unsigned: sign and install it yourself (AltStore, SideStore, Feather, Sideloadly…), then put your disc image in Files → On My iPhone → Party Board. Easiest: add the source \`$source\` in AltStore, SideStore or Feather to install and get updates |"
    echo
    echo "Other platforms: build from source (README)."
    echo
    echo "## What you need"
    echo
    echo "Your own disc image of *Mario Party 4* (USA, GMPE01). **No game data is included.**"
    echo
    echo "## New in $version"
    echo
    if [ -n "$notes" ]; then cat "$notes"; elif [ -n "$last" ]; then git log --format='- %s' "$last..HEAD" --no-merges | cut -c1-200; else echo "- First release."; fi
} >"$body"
git push -q origin HEAD
gh release create "$tag" -R $repo --target "$(git rev-parse HEAD)" --title "Party Board $version" \
    --notes-file "$body" $pre $publish "$out/partyboard-ios.ipa" "$out/altstore-source.json"
[ "$publish" = --latest ] && { gh workflow run source -R FIZL0/apps || echo "FIZL0/apps did not rebuild; it will within the hour" >&2; }
echo "$tag: $([ "$publish" = --draft ] && echo "draft, publish it on GitHub" || echo published)"
