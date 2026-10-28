# N86: unsigned iOS IPA built on Linux

`tools/ios-ipa.sh` writes `build/partyboard-ios.ipa` (25 MB): Payload/PartyBoard.app with the executable, 93 dylibs
in Frameworks/ (libdol, aurora's libnod, the 91 RELs), Info.plist, Assets.car + PNG icons, the launch storyboard and
res/. Unsigned: the user signs it (Sideloadly/AltStore/zsign/codesign) and sideloads it.

Files:

- `tools/ios-sdk.py`: opentennis's SDK extractor (Xcode .xip -> iPhoneOS.sdk, arm64e.x1 .tbd fix), same default dir
  `~/.local/share/hst-ios`, so the SDK already there (iOS 27.1) is used as is.
- `tools/ios-build.sh`: configure + build in `build/ios-linux` through **our ios.toolchain.cmake**, with everything it
  would ask xcodebuild/xcrun for passed in (XCODE_VERSION_INT, SDK_VERSION from SDKSettings.json, CMAKE_OSX_SYSROOT_INT,
  CMAKE_DEVELOPER_ROOT, clang/clang++, llvm-libtool-darwin as BUILD_LIBTOOL, llvm-install-name-tool). Linker: rustup's
  `gcc-ld/ld64.lld` via `-fuse-ld=lld --ld-path=`, as opentennis. Deployment target 15.0 (opentennis's; the
  macOS-host preset `ios-default` keeps its 14.0).
- `tools/ios-ipa.sh`: runs ios-build.sh, stages, `llvm-strip -S -x`, icons from res/icon.png (magick), zip.
- `tools/ios-ipa-check.py`: the static check below.
- CMake/source: `cmake/pbnet.cmake`, `CMakeLists.txt`, `src/game/ovllist.c`, `platforms/ios/Info.plist.in`.

## RELs: dylibs in Frameworks/, still dlopen'd

Not linked statically: the game unloads an overlay (omDLLUnlink -> dlclose) and loads it again with fresh .data/.bss,
which static linking would lose, and the 91 RELs would need their clashing globals renamed. As dylibs they build
with no change; `src/game/ovllist.c` opens `@executable_path/Frameworks/<name>.dylib` on TARGET_OS_IPHONE (dlopen
does not search an app's Frameworks for a bare leaf name; the macOS/Linux/Windows names are unchanged). Plain
`.dylib`s rather than `.framework` wrappers: iOS loads any signed Mach-O in the bundle (Xcode itself shipped Swift's
runtime as loose dylibs in Frameworks/ until iOS 12.2), signing tools sign every Mach-O in Frameworks/, and it keeps
CMake's targets as they are. Whether dlclose really unmaps an overlay on iOS (so statics reset) is not checked
(see below); on macOS dyld does unload dylibs nothing else references.

## What it took (in the order it failed)

1. Host packages leaked into the cross build: SDL found Linux `libusb-1.0.so` ("is not a .dylib"), aurora used
   `/lib64/cmake/absl` and `zstd` (then `absl/container/flat_hash_map.h` not found). `CMAKE_IGNORE_PREFIX_PATH` with
   /usr, /lib, /lib64 did not stop absl_DIR=/lib64/cmake/absl; `CMAKE_FIND_ROOT_PATH_MODE_{LIBRARY,INCLUDE,PACKAGE}=ONLY`
   did (the toolchain only defaults them to BOTH). Dawn's iOS prebuilt and FetchContent deps still resolve.
2. Host SDL3 picked as "system": `AURORA_SDL3_PROVIDER=vendor`.
3. cc-rs (ring, blake3 in pbnet; nod's compression -sys crates) runs `xcrun` for the SDK unless `SDKROOT` is set.
   On a Linux host the host build scripts ignore SDKROOT, so ios-build.sh exports it (pbnet.cmake's comment about
   not setting it is about macOS hosts).
4. pbnet's prelink is `xcrun ld -r -exported_symbol _pbnet_*`; ld64.lld has no `-r`. It exists to hide pbnet's Rust
   std from nod's static Rust std. With `AURORA_NOD_LINKAGE=shared` nod is its own dylib (one Rust std per image), so
   pbnet.cmake now links the cargo archive as is for APPLE + shared nod, as it already did for MSVC.
5. `CMAKE_SHARED_LINKER_FLAGS` from the command line never reached the dylib links: ios.toolchain.cmake sets that
   variable itself (the Frameworks rpaths), so /usr/bin/ld ran ("unrecognised emulation mode: llvm"). ios-build.sh
   writes `build/ios-linux/ios-linux.toolchain.cmake` that includes ours and appends the lld flags.
6. libdol link: `PartyBoard_iOS_ShowFileSelect` undefined: `src/port/ios/FileSelectDialog.m` was in the executable,
   but file_select.cpp in dol calls it (a Mach-O dylib can't take symbols from the app). Moved to dol (CMakeLists'
   IOS block, with UIKit/UniformTypeIdentifiers). And 19 `nw_*` undefined from netdev's iOS code: pbnet links
   `-framework Network` on IOS.
7. Bundle: CMake left rpaths `$ORIGIN` and the build dir; IOS now uses `@executable_path/Frameworks` with
   CMAKE_BUILD_WITH_INSTALL_RPATH. nod's install name was its absolute cargo path (rustc only writes
   `@rpath/<file>` with `-C rpath`): ios-build.sh adds `-C rpath`, and ios-ipa.sh deletes the one rpath that adds to
   libnod (into the build machine's rustup dir). lld warns "duplicate -rpath @executable_path/Frameworks" on every
   dylib (toolchain flag + CMAKE_INSTALL_RPATH); harmless.
8. Icon: Info.plist.in named `AppIcon` but platforms/ios/Assets.car (actool, icon_to_car.sh) holds `PartyBoard`, so
   CFBundleIconName is `PartyBoard` now; AppIcon60x60@2x/@3x and AppIcon76x76@2x~ipad PNGs are added for
   CFBundleIconFiles. Info.plist.in also gets UIFileSharingEnabled, LSSupportsOpeningDocumentsInPlace and
   NSLocalNetworkUsageDescription (iroh's mDNS/LAN discovery).

Config and saves are already in Documents on iOS (`portmain.cpp` calculate_config_path), which these keys show in
Files as On My iPhone > PartyBoard; put the ISO there and pick it in the game's disc picker. SDL chdirs to the bundle
on iOS (SDL_uikitappdelegate.m), so the game's relative `res/...` paths resolve. Safe area: the touch pad (N70) and
menus take insets from `SDL_GetWindowSafeArea`, which SDL implements on UIKit; nothing iOS-specific was needed.

## Check (no device)

`tools/ios-ipa-check.py` on the IPA: 94 Mach-O files (exe + 93 dylibs), all arm64, LC_BUILD_VERSION iOS minos 15.0,
every rpath inside the app, every LC_LOAD_DYLIB found (ours in Frameworks/, system ones as a .tbd in the SDK), and
all 15173 imports exported by the dylib their ordinal names (ours: `llvm-nm -gU`; system: the .tbd plus re-exported
.tbds), including dyld's weak-definition lookups (OSReport is DECL_WEAK; libdol has it with WEAK_DEFINES; operator
new/delete from libc++). No flat/dynamic lookups, no missing weak imports. All 91 overlays of include/ovl_table.h
that files.cmake builds have `Frameworks/<name>.dylib` exporting `_ObjectSetup` (8 entries have no source and no
build on any platform: m300Dll m302Dll m303Dll m330Dll m333Dll msetupDll nisDll safDll). The exe has LC_MAIN, PIE,
`_main`/`SDL_RunApp`. Imports: libdol from 30 libraries (UIKit, Metal, Network, Security, SystemConfiguration,
libnod, libc++, ...), each REL from libdol + libnod + the system set.

The checker itself parses LC_DYLD_CHAINED_FIXUPS: `llvm-objdump --macho --dyld-info` (LLVM 23) misnames imports for
libdol's DYLD_CHAINED_IMPORT_ADDEND64 table (448 binds shown as "libSystem _objc_msgSend (weak import)", which would
have been NULL pointers at run time; the table itself has none). Negative test: an IPA with libnod deleted and
bootDll copied over libdol fails with 93 "needs @rpath/libnod.dylib" and thousands of "not exported by libdol.dylib".

Desktop: `tools/check.sh` (Linux build) passes; the changes are under IOS / TARGET_OS_IPHONE / APPLE+shared nod.

## Not verified

- **Not run on a device or simulator**: there is no iPhone, Mac or simulator here. Launch, Metal/Dawn rendering,
  touch pad and safe area on a notch, audio, the disc picker, Files app visibility, netplay on iOS: all unrun.
- Signing: the IPA is unsigned (no Apple identity here). Not checked that a signing tool accepts it, nor that
  installd accepts loose dylibs in Frameworks/ from a sideload tool (it did for Xcode's Swift dylibs).
- dlclose unloading an overlay on iOS (statics reset on reload) is not checked; if dyld keeps it mapped, a REL would
  keep its .data from the last load (desktop builds rely on the unload).
- The disc picker uses `UIDocumentPickerViewController ... asCopy:YES`, so picking an ISO already in Documents copies
  it (1.4 GB) into the app's tmp, which iOS may purge; filed as follow-up N96, not changed blind.
- LAN discovery: iroh's mDNS on iOS needs the multicast entitlement (com.apple.developer.networking.multicast) that a
  free signing identity can't grant; internet play (relay, DHT codes) doesn't need it. Not checked either way.
- The macOS-host path (preset ios-default with Xcode) was not run: no Mac. It uses the same CMake changes; its
  pbnet prelink is unchanged unless AURORA_NOD_LINKAGE=shared.
- Icons: the Assets.car was compiled by an earlier actool for the `.icon` format; whether iOS 15-17 render it, or
  fall back to the PNGs, is not checked.

## Second session (merge of the saved work)

The first session ended before committing (its work was saved on wip/N86, with a stray 18 MB `librust_out.a` at the
repo root, a rustc probe leftover, which is not committed). Its TODO line for the disc picker used ID N90, which is
now the web size task, so the follow-up is filed as **N96** through `run plan add`. Rebuilt on top of main 57be4156
(N93/N94 merged): `tools/ios-ipa.sh` writes the 25 MB IPA with 93 dylibs in Frameworks/, and `tools/ios-ipa-check.py`
passes again (94 Mach-O files, 15173 imports all resolved, 91 overlays export `_ObjectSetup`); Info.plist has
UIFileSharingEnabled, LSSupportsOpeningDocumentsInPlace and MinimumOSVersion 15.0.
