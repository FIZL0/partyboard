# N100: iOS disc picker opens the ISO in place

## Problem
`src/port/ios/FileSelectDialog.m` used `UIDocumentPickerViewController initForOpeningContentTypes:asCopy:YES`.
Picking the ISO from Documents (Files > On My iPhone > PartyBoard, N86) copied 1.4 GB into the app's tmp, and the
saved `backend.isoPath` pointed at that copy, which iOS may purge.

## What changed
- Picker: `asCopy:NO`. On pick, `startAccessingSecurityScopedResource` (held for the whole run, never stopped:
  the disc stays open while the game runs) and a bookmark (`bookmarkDataWithOptions:0`; on iOS bookmarks of
  picked URLs carry the security scope implicitly, `NSURLBookmarkCreationWithSecurityScope` is macOS-only) saved
  in `NSUserDefaults` under `partyboard.bookmark.<path>`. The callback still gets the plain path, so prelaunch
  and config are unchanged.
- New `PartyBoard_iOS_RestoreFileAccess(path, out, size)` (FileSelectDialog.h), called in `portmain.cpp` before
  the saved path is validated, iOS only:
  1. bookmark for the saved path → resolve, start access, re-save if stale or moved, return its current path;
  2. else the saved path if readable (a file in our own container needs no scope);
  3. else the saved path rebased onto `NSHomeDirectory()` after `.../Data/Application/<UUID>/`: the container
     UUID changes on app update/reinstall, which would otherwise make a Documents path stale too.
  If the path differs, `isoPath` is updated (and saved later as usual). If nothing is found, the existing check
  clears the path and shows the prelaunch UI, as before.
- Offline/other platforms: untouched (code is under `TARGET_OS_IOS`).

## Checks
- `tools/ios-build.sh`: FileSelectDialog.m and portmain.cpp compile for arm64-apple-ios15.0 with no new warnings.
- `tools/ios-ipa.sh` + `tools/ios-ipa-check.py build/partyboard-ios.ipa`: 94 Mach-Os, 15176 imports all
  resolve (15173 in N86).
- `tools/check.sh` (Linux build) passes.

## Not verified
- **Everything on a device**: no iPhone/iPad or simulator here (Linux host, no Xcode runtime). Unchecked:
  that the picker shows and opens Documents files in place without a copy; that `startAccessingSecurityScopedResource`
  + the bookmark keep an ISO from iCloud Drive / another app's provider readable across relaunches; that the
  container rebase picks the ISO up after an app update; that iOS's `/private/var` vs `/var` prefixes don't
  make a resolved bookmark path differ from the saved one (harmless if they do: it is just re-saved).
- Bookmarks for earlier picks are never removed from NSUserDefaults (a few hundred bytes each); not worth the
  code until someone picks many discs.
