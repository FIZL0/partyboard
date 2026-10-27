# N17: pbnet on tvOS

Follow-up to `2026-10-27-n13-pbnet-non-linux`. The `tvos-default` preset passes `Rust_CARGO_TARGET=aarch64-apple-tvos`
to `cmake/pbnet.cmake`. The task said the pbnet cargo step fails there because the target is tier 3 with no prebuilt
std on stable. The fix it suggested was nightly + `-Zbuild-std`, or skipping pbnet on tvOS.

## Findings

- **The task's premise is out of date.** `rustup target list` shows `aarch64-apple-tvos` (and `-sim`) for stable
  1.99 and nightly 1.101 (2026-10-18). `rustup target add --toolchain stable aarch64-apple-tvos` installs a
  prebuilt `rust-std`. A missing std is not the problem.
- **The real blocker is a dependency with no tvOS code.** `cargo build --release --locked --target
  aarch64-apple-tvos` on stable:
  1. ring's build script needs `xcrun` / an Apple SDK, which this Linux host doesn't have (expected; a Mac has
     one). With `SDKROOT` set to the local iPhoneOS.sdk (`~/.local/share/hst-ios/iPhoneOS.sdk`, same libc headers)
     and `CC_aarch64_apple_tvos=clang`, ring compiles.
  2. Then **`netdev` 0.46.3 fails** (pulled in by `iroh 1.3 -> netwatch 0.19.3 -> netdev`):
     - `src/os/darwin/ipv6_addr_flags.rs`: `libc::in6_ifreq` not found (`libc` 0.2.191 does not define it for tvOS).
     - `src/interface/mod.rs` `interfaces()` has `cfg` branches for linux/android/windows/macos/ios/bsd only, so on
       tvOS its body is `()`.

  This is a missing-code problem in a crate, not a toolchain one, so nightly + `-Zbuild-std` would fail the
  same way. Making pbnet build for tvOS needs netdev (and probably netwatch) to support `target_os = "tvos"`
  upstream, or a fork patched in with `[patch]`. That is out of scope here; filed as N19.

## What was done

Skip the crate on tvOS and link a C stand-in, so the tvOS build links and plays offline:

- `src/port/pbnet_stub.c`: every function in `include/port/pbnet.h`, behaving as the crate does with no session
  (`pbnet_lockstep_active` 0, `pbnet_step` `PBNET_OFF`, `pbnet_poll_desync` 0, ...). `pbnet_version` returns
  `"none (not built for this target)"`, so the boot log shows it. `pbnet_delay_for` uses the same double
  arithmetic as `lockstep.rs`.
- `cmake/pbnet.cmake`: new `PBNET_STUB` option, default ON when the cargo target ends in `-tvos` / `-tvos-sim`.
  When ON, `pbnet::pbnet` is an alias of the stub library and the rest of the file (cargo step, pre-link) is
  skipped with `return()`.
- The stub library is built on every platform, so the Linux gate compiles it against the header (catches
  signature drift).
- Configure checks that every `pbnet_*(` in `pbnet.h` is defined in the stub: a WARNING normally, FATAL_ERROR when
  the stub is in use. It is a warning on desktop so another task that adds a C ABI function doesn't break the
  Linux gate; it does tell them to add the stub.
- Why a stub library and not `#ifdef`s around callers: today only `portmain.cpp` calls `pbnet_version`, but the
  netplay work will add callers in `src/port`. With a stub, none of them need a tvOS guard.

## Tried / failed

- First version of the header/stub check put `(` straight into a CMake regex (`file(STRINGS ... REGEX)`), which
  is an unmatched-paren compile error. Fixed by matching the bare name plus `\\(`.

## Verified

- `tools/check.sh` (cargo tests + Linux desktop build) passes.
- Full Linux build with `-DPBNET_STUB=ON` in a scratch build dir (deleted afterwards): configure + build of all 823
  steps succeeds. No `pbnet-cargo` dir is created. `libdol.so` exports all 9 `pbnet_*` from the stub and has
  the stub version string. Its only `rust_eh_personality` symbols are nod's (2).
- Sync check: renaming one stub function gave the WARNING naming `pbnet_waiting_on()`; restored.
- `cargo build --target aarch64-apple-tvos` on stable gets to the netdev errors above (with the iPhoneOS.sdk stand-in
  for ring).

## Not verified

- **The tvOS configure/build itself** (`cmake --preset tvos-default`): needs macOS + Xcode with the AppleTV SDK
  (`ios.toolchain.cmake`), and this is a Linux host. So the `-tvos` match on `Rust_CARGO_TARGET` is checked only by
  reading the preset (`"Rust_CARGO_TARGET": "aarch64-apple-tvos"`), not by a tvOS configure.
- Whether `ios.toolchain.cmake` with `PLATFORM=TVOS` compiles the stub (plain C99 + `<math.h>`) and links `ceil`
  from libSystem: expected, not run.
- netdev on nightly + `-Zbuild-std`: not run. The errors are `cfg` gaps in the crate's source, which the toolchain
  doesn't change.
- Running the game (any platform): no disc image in `orig/GMPE01_00/`, and nothing here changes runtime behaviour
  off tvOS.
