#!/usr/bin/env bash
# tools/ios-source-check.sh SOURCE.json|URL — fails unless every app's versions[] is newest first (by version number),
# has no duplicate version, has more than one entry, and its first entry matches the app's top-level
# version/versionDate/downloadURL/size. tools/ios-source.sh runs it on every source it writes.
set -euo pipefail
[ $# -eq 1 ] || { echo "usage: tools/ios-source-check.sh SOURCE.json|URL" >&2; exit 1; }
case $1 in http*) json=$(curl -fsSL "$1") ;; *) json=$(cat "$1") ;; esac
jq -e '
  def key: split("-")[0] | split(".") | map(tonumber);
  .apps[] | .name as $n | (.versions | map(.version)) as $vs
  | if ($vs | length) < 2 then error("\($n): versions[] has \($vs | length) entry")
    elif ($vs | unique | length) != ($vs | length) then error("\($n): duplicate version in versions[]")
    elif [range(1; $vs | length) | select(($vs[. - 1] | key) < ($vs[.] | key))] != [] then error("\($n): versions[] not newest first: \($vs)")
    elif [.version, .versionDate, .downloadURL, .size] != (.versions[0] | [.version, .date, .downloadURL, .size])
      then error("\($n): top-level fields do not match versions[0]")
    else true end' <<<"$json" >/dev/null
