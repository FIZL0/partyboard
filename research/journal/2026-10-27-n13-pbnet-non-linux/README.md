# N13: pbnet on Windows, macOS/iOS and Android

Follow-up to `2026-10-27-n1-pbnet-crate`. The question everywhere: `dol` already links aurora's nod (a Rust
library with its own std). Does pbnet's second std clash with it, and if so, does the hiding step work?

## What decides the clash: how aurora links nod

`extern/aurora/CMakeLists.txt` defaults `AURORA_NOD_LINKAGE` to `shared` on WIN32 and `static` everywhere
else. `AURORA_NOD_PROVIDER=auto` downloads a prebuilt package for windows-x86_64, linux-x86_64 and
macos-arm64, and builds nod from source with corrosion everywhere else (Android, iOS, macOS x86_64).

| Platform | nod in `dol` | Clash without hiding? | pbnet handling |
|---|---|---|---|
| Linux x86_64 | prebuilt `libnod.a` | yes (`rust_eh_personality`) | `ld -r` + `objcopy` (N1) |
| Android | built from source, static, **same rustc as pbnet** | yes | same ELF path, NDK `ld.lld` + `llvm-objcopy` |
| macOS arm64 | prebuilt `libnod.a` | yes: `_rust_eh_personality` + ~340 compiler-builtins symbols | `xcrun ld -r -exported_symbol "_pbnet_*"` |
| iOS | built from source, static | yes (same as macOS) | same as macOS |
| Windows MSVC | `nod.dll` via import lib `nod.lib` | **no** | cargo `pbnet.lib` linked as is |
| Windows MSVC, `AURORA_NOD_LINKAGE=static` | `nod_static.lib` | yes (`rust_eh_personality`) | refused at configure (FATAL_ERROR) |

## What was done (`cmake/pbnet.cmake`)

- MSVC: still links the cargo `.lib` as is. The comment now says why that is fine, and configure fails
  with a clear message if `AURORA_NOD_LINKAGE=static`. That setup fails at link time anyway, and COFF has
  no `ld -r` + localize step to fix it.
- MSVC target triple from `CMAKE_C_COMPILER_ARCHITECTURE_ID` (x64/ARM64/X86). Before, it used the host
  triple, which gives the wrong lib for the `windows-arm64-msvc` preset on an x64 host.
- Windows link libs: added `dbghelp` (rustc 1.99 `--print native-static-libs` for x86_64-pc-windows-msvc:
  `kernel32 ntdll userenv ws2_32 dbghelp /defaultlib:msvcrt`). Kept `bcrypt advapi32` (harmless, older std).
- `PBNET_CARGO_TARGET` falls back to `Rust_CARGO_TARGET` when a preset sets it (ios-default, tvos-default,
  x-android-ci-arm64 set it for corrosion), so pbnet and nod are built for the same triple.
  Corrosion itself only fills `Rust_CARGO_TARGET_CACHED`, so desktop presets keep the host default.
- iOS simulator on x86_64 maps to `x86_64-apple-ios` (it went to `aarch64-apple-ios-sim` before).
- macOS with a multi-arch `CMAKE_OSX_ARCHITECTURES` now fails at configure. Before, it silently built
  only the host arch. A universal build would need one cargo build per arch plus `lipo`. No preset does
  this (the CI presets are per-arch).

## Findings

- **Windows default links clean.** Test link with the NDK's `lld-link` (no MSVC here): a COFF `main.obj`
  calling `pbnet_version` and `nod_disc_open`, plus cargo `pbnet.lib` and the real
  `libnod-windows-x86_64.tar.gz` (v2.0.0-alpha.10) files, with `/nodefaultlib /force:unresolved`:
  - with `nod.lib` (import lib, the default): no duplicate symbols.
  - with `nod_static.lib`: `duplicate symbol: rust_eh_personality`, the only duplicate reported. The
    other ~450 names both libs define are weak compiler-builtins, COMDAT string literals and
    raw-dylib import stubs, which COFF allows.
- **CRT on MSVC.** The Rust staticlib carries no `/DEFAULTLIB` directives (`llvm-readobj --coff-directives`
  shows none, with or without `+crt-static`). `/defaultlib:msvcrt` is only rustc's hint to whoever links it,
  so `dol` uses whatever CRT `CMAKE_MSVC_RUNTIME_LIBRARY` picks (`MultiThreaded` in relwithdebinfo).
  No `RUSTFLAGS=-Ctarget-feature=+crt-static` needed.
- **Android, same rustc.** On Android nod is built from source by the same stable rustc (1.99) as pbnet,
  so the two std copies have identical mangled names, not only the unmangled ones. The ELF hiding still
  works: in `libdol.so`, pbnet's `rust_eh_personality` is local (`t`), nod's is global (`T`), and both
  `DW.ref.rust_eh_personality` copies exist as locals. Every `pbnet_*` is in the dynamic symbol table.
- Android link libs `dl log m` were enough. rustc also lists `unwind` and `c`, which the NDK clang driver
  links by default.

## Tried / failed

- Running `ld64.lld -r` (from the NDK) to try the Apple pre-link on Linux: `Option '-r' is not yet
  implemented`. No ld64/cctools here, so the Apple step was only checked by symbol comparison (above).
- First configure harness run gave empty triples everywhere: my harness split its fake variables wrong.
  It was a harness bug, not a pbnet.cmake bug.

## Verified

- `cmake --preset android-arm64 && cmake --build --preset android-arm64` (NDK 28.2.13676358, android-28):
  built `libmain.so` and `libdol.so` with pbnet hidden as above. Reconfigured with the final pbnet.cmake:
  builds, and does not relink when the crate didn't change.
- `cmake --preset android-x86_64 && cmake --build --preset android-x86_64`: builds; same result in `libdol.so`
  (9 `pbnet_*` exported, pbnet's `rust_eh_personality` local, nod's global). Reconfigured with the final file too.
- `cargo build` of the crate for x86_64-pc-windows-msvc, aarch64-apple-darwin, aarch64-apple-ios and
  aarch64-linux-android (staticlib needs no linker).
- Configure harness (a CMake project faking `MSVC`/`APPLE`/`IOS`/`ANDROID` variables, then including
  `cmake/pbnet.cmake`) gives these triples: MSVC x64 -> x86_64-pc-windows-msvc, ARM64 -> aarch64-pc-windows-msvc,
  static nod -> FATAL_ERROR, macOS arm64 -> aarch64-apple-darwin, macOS `x86_64;arm64` -> FATAL_ERROR,
  iOS sim x86_64/arm64 -> x86_64-apple-ios / aarch64-apple-ios-sim, iOS device -> aarch64-apple-ios,
  `Rust_CARGO_TARGET` wins, Android x86_64 -> x86_64-linux-android, Linux -> host.
- `tools/check.sh` (Linux desktop).

## Not verified

- **Real MSVC link** (`link.exe`, Windows SDK libs): no Windows machine or MSVC libs here. Only checked with
  lld-link and `/nodefaultlib /force:unresolved`, which shows duplicates but not whether every system
  lib pbnet needs is in `INTERFACE_LINK_LIBRARIES`. The arm64 MSVC triple mapping and `nod.dll` being
  copied next to the exe were not checked either.
- **`xcrun ld -r -all_load -exported_symbol "_pbnet_*"`** on macOS/iOS: never run (no Apple ld here; ld64.lld
  has no `-r`). The macOS clash it must fix is confirmed by symbols. Whether ld64 (incl. Xcode 15+
  ld-prime) turns the hidden symbols into locals in `-r` mode is from the ld64 man page, not a test.
  The macOS/iOS builds as a whole were not configured either.
- tvOS: `Rust_CARGO_TARGET=aarch64-apple-tvos` now reaches pbnet, but that is a tier-3 target. Stable
  has no prebuilt std for it, so the pbnet cargo step would fail there (the preset uses nightly for nod via
  `Rust_TOOLCHAIN`). Out of scope; filed as N14.
- Android armeabi-v7a / x86: no preset, rust targets not installed; only the mapping exists.
- Running the Android build on a device or emulator: not done (no disc image in `orig/GMPE01_00/`, and only
  linking was in scope).
