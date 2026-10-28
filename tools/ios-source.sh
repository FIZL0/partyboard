#!/usr/bin/env bash
# tools/ios-source.sh VERSION IPA [OUT] — the iOS app as a sideloading source, as opentennis's tools/ios-source.sh: the
# AltStore v2 format (AltStore, SideStore, Feather), pointing at release vVERSION's copy of IPA. OUT defaults to
# altstore-source.json next to IPA. tools/release.sh uploads it with every release, so
# https://github.com/FIZL0/partyboard/releases/latest/download/altstore-source.json is always the latest's; the shared
# FIZL0 source (github.com/FIZL0/apps, https://fizl0.github.io/apps/source.json) merges it with the other apps'.
# The app also carries the legacy top-level version/downloadURL fields, for older signers that predate `versions`.
set -euo pipefail
[ $# -ge 2 ] || { echo "usage: tools/ios-source.sh VERSION IPA [OUT]" >&2; exit 1; }
version=${1#v} ipa=$2 out=${3:-$(dirname "$2")/altstore-source.json}
[ -f "$ipa" ] || { echo "no such IPA: $ipa" >&2; exit 1; }
repo=FIZL0/partyboard
icon=https://raw.githubusercontent.com/$repo/main/res/icon.png
# minOSVersion: tools/ios-build.sh's min; the bundle id: CMakeLists.txt's PARTY_BOARD_BUNDLE_IDENTIFIER
jq -n --arg v "$version" --arg date "$(date -u +%Y-%m-%d)" \
    --arg ipa "https://github.com/$repo/releases/download/v$version/partyboard-ios.ipa" \
    --argjson size "$(stat -c %s "$ipa")" --arg icon "$icon" \
    --arg src "https://github.com/$repo/releases/latest/download/altstore-source.json" '
  { name: "Party Board", identifier: "com.mariopartyrd.partyboard", sourceURL: $src,
    website: "https://github.com/FIZL0/partyboard", iconURL: $icon, tintColor: "#e60012",
    subtitle: "Mario Party 4 (GameCube) with online play",
    apps: [{ name: "Party Board", bundleIdentifier: "com.mariopartyrd.partyboard", developerName: "FIZL0",
             subtitle: "Mario Party 4 (GameCube) with online play",
             localizedDescription: "A PC/mobile port of the Mario Party 4 decompilation, with online play. Plays from your own disc image (USA, GMPE01): put it in Files → On My iPhone → Party Board.",
             iconURL: $icon, tintColor: "#e60012", category: "games",
             version: $v, versionDate: $date, downloadURL: $ipa, size: $size,
             versions: [{ version: $v, date: $date, downloadURL: $ipa, size: $size, minOSVersion: "15.0" }] }],
    news: [] }' >"$out"
echo "$out"
