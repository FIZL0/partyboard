# N12: pbnet on Android (Gradle) and iOS; online menu by touch

## What was done

- **Gradle builds the native side** (`platforms/android/app/build.gradle`). Before this, `./gradlew assembleDebug`
  packaged whatever `.so` files someone had staged by hand. With no staged files it built an APK with no native code.
  New tasks: `configureAndroid<Abi>` (`cmake --preset android-*`, only if `build/<preset>/build.ninja` is missing),
  `buildAndroid<Abi>` (`cmake --build --preset`, which runs pbnet's cargo cross-build via `cmake/pbnet.cmake`),
  `stageNativeLibs` (the existing `scripts/stage-jni-libs.sh` / `.bat` with `ANDROID_STAGE_ABIS`), and the umbrella
  `buildNativeLibs`. `preBuild` depends on it.
  Properties: `partyboard.nativeAbis` (default `arm64-v8a`), `partyboard.skipNative`, `partyboard.cmake`.
  `ANDROID_HOME` / `ANDROID_NDK_VERSION` default to Gradle's SDK dir and its newest NDK. README updated.
- **iOS: the crate did not compile for `aarch64-apple-ios`.** `iroh 1.3 -> netwatch 0.19.3 -> netdev 0.46.3`
  `src/os/darwin/ipv6_addr_flags.rs` uses `libc::in6_ifreq`. libc 0.2.190 put that struct (and
  `SIOCGIFAFLAG_IN6`) behind `cfg(target_os = "macos")`; 0.2.189 had it for every Apple target. The lock had 0.2.191.
  N13 had built for iOS successfully before libc moved. Fix: `net/Cargo.toml` pins
  `libc = "=0.2.189"` under `cfg(target_os = "ios")`. Cargo unifies one libc 0.2 for the whole graph, so this pins it
  everywhere, and it can't be undone by a stray `cargo update`. opentennis ships iOS on 0.2.189.
  The same libc change is very likely the `in6_ifreq` half of N17/N19's tvOS failure. The other half (`interfaces()`
  has no tvOS branch) remains; not tried here.
- `cmake/pbnet.cmake`: on iOS, cargo gets `IPHONEOS_DEPLOYMENT_TARGET` from `CMAKE_OSX_DEPLOYMENT_TARGET` (14.0 in the
  preset), so the Rust objects and ring's C match the app's minimum. An env var set at configure time wins.
  Deliberately **not** `SDKROOT`: cargo also builds host (macOS) build scripts, and those must not get the iPhoneOS SDK.
- **Online menu, mobile fixes** (`src/port/ui/online.cpp`):
  - It did not compile for Android: `std::chrono::clock_cast` is missing from the NDK's libc++ (and Apple's).
    Replaced with a `file_clock::now()` / `system_clock::now()` offset (`file_time_to_system`).
    The Linux gate didn't catch it because libstdc++ has `clock_cast`.
  - Text fields edited the *display* text. Code showed `(none)` or a 20-char `abc...`, and Relay URL did the same at
    24 chars. Pressing a field put that text in the input (and the phone keyboard), so confirming a long code unchanged
    saved it truncated. `TextButton` now takes a `getInput` getter: the input starts with the full value (empty if none).
- **Android Java SDL shim synced** (`scripts/sync-sdl-java.sh` from the SDL 3.4.10 the build fetches). The repo's
  copy was SDL 3.2.4. The native SDL registers `SDLControllerManager.onNativePadDown(III)Z`, which the old Java lacked,
  so **every APK aborted in `System.loadLibrary`** with a JNI NoSuchMethodError before `main`. Found on the
  emulator; after the sync the app starts. (Unrelated to netplay, but no Android test of anything is possible
  without it.)

## Touch, by reading the code

Touch goes through aurora (`extern/aurora/lib/rmlui.cpp`): fingers become RmlUi touch and mouse events. A tap is a
Click, and `Component::on_nav_command` maps Click to Confirm. So in the online menu:

- Buttons (Host, Join, Paste Code, Copy, Start, Leave) and the tab bar's close button: tap.
- Select buttons with a list (seats, characters, board, relay): a tap focuses the button and shows the list in the
  right pane, then you tap an entry. The Left/Right character and board cycling is pad/keys only, but the list
  covers it.
- Number fields (turns, handicaps, players): a tap opens the number keyboard. pbnet rounds turns down to a step of 5.
- Host's code: a tap copies it.

## Checked

- `./gradlew :app:assembleDebug` (JDK 21 Temurin in the scratchpad; the system JDK 27 is too new for Gradle 8.13),
  from a clean Gradle state:
  - arm64 (existing `build/android-arm64`): built, staged 93 `.so`, APK 70 MB. `libdol.so` exports 21 `pbnet_*`, one
    global `rust_eh_personality` (nod's). The dex has `org/rustls/platformverifier`, and `aapt` shows INTERNET.
  - `-Ppartyboard.nativeAbis=arm64-v8a,x86_64` with no `build/android-x86_64`: the configure task ran, Dawn built
    from source, and both ABIs were staged and packaged.
- `cargo build --release --locked --target aarch64-apple-ios`. Host toolchain stand-ins: the iPhoneOS.sdk copy at
  `~/.local/share/hst-ios`, `CC=clang`, and a linker wrapper `rust-lld -flavor darwin` (iroh-relay also builds a
  cdylib). libc 0.2.191 fails at netdev `in6_ifreq`; 0.2.189 builds `libpbnet.a` with 20 `pbnet_*`.
- `cmake/pbnet.cmake` iOS env, with a harness (a tiny project faking `IOS`/`APPLE` that includes the file): the cargo
  command gets `IPHONEOS_DEPLOYMENT_TARGET=14.0`; it is left out when the env var is preset or when not iOS.
- Android emulator (AVD hst34, Android 14 x86_64, `-gpu host -feature Vulkan`, read-only), with a temporary,
  reverted `push_document(OnlineWindow)` after the prelaunch push in `portmain.cpp`: the app starts and stays up
  with the Online window pushed.
  - Before the SDL Java sync, it aborted at load.
  - With `-gpu swiftshader_indirect`, `aurora_initialize` aborts: Dawn has no adapter.
- `tools/check.sh` passes.

## Tried and failed

- Fake SDK headers (stub `TargetConditionals.h`, `assert.h`) for ring: replaced with the real SDK copy once found.
- Linking iroh-relay's cdylib with host `cc` (GCC: no `-framework`), then `clang -fuse-ld=lld`: no `ld64.lld` here.
  Rustc treats a wrapper with `ld` in its name as ld64-flavoured, so `rust-lld -flavor darwin` worked.
- Screenshots on the emulator: `adb exec-out screencap` and `adb emu screenrecord screenshot` are both black with
  `-gpu host` headless. Only the system "Viewing full screen" overlay showed, and a tap on its GOT IT worked.
- The background Gradle log was moved mid-run once, which cut it off; the build was re-run.

## Not verified

- **The online menu by touch, by eye**: no frame could be captured from the emulator (above), so taps on the Online
  tabs, the soft keyboard on the Code field and the lobby lists were not seen. Filed as **N30**.
- **Soft-keyboard autocorrect**: aurora starts text input with SDL defaults (autocorrect on, sentence caps). Caps are
  harmless (pbnet upper-cases codes), but autocorrect could rewrite a typed 4-symbol code. Paste Code avoids it.
  The fix needs an aurora `InputType` (submodule, upstream repo). Filed as **N31**.
- **iOS app build** (`cmake --preset ios-default`, `xcrun ld -r` pre-link, Xcode link, device run): no macOS/Xcode
  here. Only the crate's cross-compile and the CMake env were checked. `aarch64-apple-ios-sim` was not built (rust
  target not installed; the preset is device-only).
- **The arm64 APK on a device**: no arm64 device or emulator image; only the x86_64 APK ran.
- **Netplay itself on Android** (hosting/joining a lobby from the phone, TLS verifier init): not exercised in the
  emulator run; the window was only pushed.
- **Game on Android**: no disc image in `orig/GMPE01_00/`, so the app stays in the prelaunch UI.
- **Gradle on a Windows host** (`stage-jni-libs.bat` path, `cmd /c`): no Windows host.
- **The other SDL Java changes** from the sync (HID, sensor manager, input connection): they are upstream SDL
  3.4.10's own, and were not exercised beyond start-up.
