#!/usr/bin/env bash
# tools/ios-source.sh VERSION IPA [OUT] [BODY] — the iOS app as a sideloading source, as opentennis's tools/ios-source.sh:
# the AltStore v2 format (AltStore, SideStore, Feather), pointing at release vVERSION's copy of IPA. OUT defaults to
# altstore-source.json next to IPA; BODY is vVERSION's release notes (tools/release.sh's notes.md). tools/release.sh
# uploads it with every release, so https://github.com/FIZL0/partyboard/releases/latest/download/altstore-source.json
# is always the latest's; the shared FIZL0 source (github.com/FIZL0/apps, https://fizl0.github.io/apps/source.json)
# merges it with the other apps'.
# versions[] lists every published release with an IPA (newest first, from the GitHub API: drafts and pre-releases
# left out) under the one being built, so clients show history and can roll back; a failed API call fails the release.
# The app also carries the legacy top-level version/downloadURL fields (= versions[0]), for older signers.
set -euo pipefail
[ $# -ge 2 ] || { echo "usage: tools/ios-source.sh VERSION IPA [OUT] [BODY]" >&2; exit 1; }
version=${1#v} ipa=$2 out=${3:-$(dirname "$2")/altstore-source.json} body=${4:-/dev/null}
[ -f "$ipa" ] || { echo "no such IPA: $ipa" >&2; exit 1; }
repo=FIZL0/partyboard
icon=https://raw.githubusercontent.com/$repo/main/res/icon.png
# gh reads GH_TOKEN/GITHUB_TOKEN when set (CI); --paginate prints one array per page
releases=$(gh api "repos/$repo/releases" --paginate | jq -s add) || { echo "GitHub releases API failed" >&2; exit 1; }
# minOSVersion: tools/ios-build.sh's min; the bundle id: CMakeLists.txt's PARTY_BOARD_BUNDLE_IDENTIFIER
# today, or the release's own date when regenerating a published one
date=$(jq -r --arg t "v$version" 'map(select(.tag_name == $t and .published_at))[0].published_at[:10] // (now | todate[:10])' <<<"$releases")
jq -n --arg v "$version" --arg date "$date" \
    --arg ipa "https://github.com/$repo/releases/download/v$version/partyboard-ios.ipa" \
    --argjson size "$(stat -c %s "$ipa")" --rawfile body "$body" --argjson releases "$releases" --arg icon "$icon" \
    --arg src "https://github.com/$repo/releases/latest/download/altstore-source.json" '
  # the "## New in X" section of a release body, trimmed
  def notes: (. // "") | gsub("\r"; "") | ("\n" + .) | split("\n## ") | map(select(startswith("New in"))) | first // ""
    | sub("^[^\n]*"; "") | gsub("^\\s+|\\s+$"; "");
  def entry($v; $date; $url; $size; $body): { version: $v, date: $date, downloadURL: $url, size: $size,
    minOSVersion: "15.0" } + ($body | notes | if . == "" then {} else { localizedDescription: . } end);
  ([entry($v; $date; $ipa; $size; $body)] + [$releases[] | select(.draft | not) | select(.prerelease | not)
     | (.tag_name | ltrimstr("v")) as $tv | select($tv != $v)
     | first(.assets[] | select(.name | endswith(".ipa"))) as $a
     | entry($tv; .published_at[:10]; $a.browser_download_url; $a.size; .body)]
   | .[:1] + (.[1:] | sort_by(.version | split("-")[0] | split(".") | map(tonumber)) | reverse)) as $versions
  | { name: "Party Board", identifier: "com.mariopartyrd.partyboard", sourceURL: $src,
    website: "https://github.com/FIZL0/partyboard", iconURL: $icon, tintColor: "#e60012",
    subtitle: "Mario Party 4 (GameCube) with online play",
    apps: [{ name: "Party Board", bundleIdentifier: "com.mariopartyrd.partyboard", developerName: "FIZL0",
             subtitle: "Mario Party 4 (GameCube) with online play",
             localizedDescription: "A PC/mobile port of the Mario Party 4 decompilation, with online play. Plays from your own disc image (USA, GMPE01): put it in Files → On My iPhone → Party Board.",
             iconURL: $icon, tintColor: "#e60012", category: "games",
             version: $versions[0].version, versionDate: $versions[0].date,
             downloadURL: $versions[0].downloadURL, size: $versions[0].size,
             versions: $versions }],
    news: [] }' >"$out"
"$(dirname "$0")/ios-source-check.sh" "$out"
echo "$out"
