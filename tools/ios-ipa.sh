#!/usr/bin/env bash
# Unsigned IPA of Party Board for the user to sign and sideload (N86), as opentennis's tools/ios-ipa.sh: build
# (tools/ios-build.sh), strip, Payload/PartyBoard.app with its Info.plist (platforms/ios/Info.plist.in), icons and res/,
# zipped. Usage: tools/ios-ipa.sh [OUT.ipa]   (default build/partyboard-ios.ipa)
#
# The RELs stay dlopen'd dylibs, now in PartyBoard.app/Frameworks with dol and aurora's nod (src/game/ovllist.c opens
# @executable_path/Frameworks/<name>.dylib); the signing tool signs every Mach-O in there.
# On the phone: Files > On My iPhone > PartyBoard is the app's Documents (config.json, saves): the disc image goes in
# there and is picked from the game's disc picker.
set -euo pipefail
cd "$(dirname "$0")/.."
out=$(realpath -m "${1:-build/partyboard-ios.ipa}")
build=${PB_IOS_BUILD:-build/ios-linux}
tools/ios-build.sh
for t in llvm-strip llvm-install-name-tool llvm-otool magick zip; do
  command -v $t >/dev/null || { echo "need $t" >&2; exit 1; }
done

stage=$build/ipa
app=$stage/Payload/PartyBoard.app
rm -rf "$stage"
mkdir -p "$stage/Payload"
cp -a "$build/PartyBoard.app" "$app"
mkdir -p "$app/Frameworks"
# The REL list is the build's: every <REL>.dylib CMake made next to libdol.dylib.
for lib in "$build"/*.dylib "$build/_deps/aurora_nod-build/libnod.dylib"; do
  llvm-strip -S -x -o "$app/Frameworks/$(basename "$lib")" "$lib"
done
llvm-strip -S -x "$app/PartyBoard"
# rustc's -C rpath (for nod's @rpath install name) also adds an rpath into the build machine's rustup dir.
nod=$app/Frameworks/libnod.dylib
llvm-otool -l "$nod" | sed -n 's/^ *path \(.*\) (offset.*/\1/p' | while read -r rp; do
  llvm-install-name-tool -delete_rpath "$rp" "$nod"
done
# Icons: the Assets.car (actool, platforms/ios/icon_to_car.sh) has "PartyBoard"; plain PNGs for CFBundleIconFiles too.
magick res/icon.png -resize 120x120 "$app/AppIcon60x60@2x.png"
magick res/icon.png -resize 180x180 "$app/AppIcon60x60@3x.png"
magick res/icon.png -resize 152x152 "$app/AppIcon76x76@2x~ipad.png"
rm -rf "$app/icons"  # a copy of Assets.car (CMake's glob of platforms/ios)

rm -f "$out"
(cd "$stage" && zip -qr -X "$out" Payload)
echo "$out ($(du -h "$out" | cut -f1), $(ls "$app/Frameworks" | wc -l) dylibs in Frameworks)"
