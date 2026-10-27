# N21: Android CMake build of pbnet

## What was done

`cmake/pbnet.cmake` now hands the NDK toolchain to cargo in the `pbnet_cargo` env on Android
(`if (ANDROID AND PBNET_CARGO_TARGET)`):

- `CC_<triple>` and `CARGO_TARGET_<TRIPLE>_LINKER`: the NDK's per-API clang wrapper
  `<dir of CMAKE_C_COMPILER>/<triple><api>-clang` (`.cmd` on a Windows host; `armv7-linux-androideabi` maps to the
  wrapper's `armv7a-` spelling). Not `CMAKE_C_COMPILER` itself: that is the NDK's plain `clang`, and the target lives
  in `CMAKE_C_COMPILER_TARGET`, which neither cc-rs's nor rustc's linker invocation would pass. The wrapper is just
  `clang --target=<triple><api>`.
- `AR_<triple>`: `CMAKE_AR` (the NDK's `llvm-ar`).
- API level: `ANDROID_PLATFORM_LEVEL` if set (NDK toolchain file), else `CMAKE_SYSTEM_VERSION` (CMake's built-in
  Android support).
- A var already in the environment at configure time is not overridden (escape hatch for odd setups).
- Wrapper missing: configure warns and names the three vars to set by hand, rather than failing.

## What failed along the way

- First cut used `CMAKE_SYSTEM_VERSION` for the API level: with the NDK's `android.toolchain.cmake` (which defaults
  to its *legacy* toolchain file) it is `1`, so it looked for `aarch64-linux-android1-clang`. The NDK exports
  `ANDROID_PLATFORM_LEVEL` (28 for the presets' `android-28`) in both its legacy and new modes; use that first.

## Checked

- `cmake --preset android-arm64` (NDK 28.2, fresh build dir, no `CC_*`/`AR_*`/`CARGO_TARGET_*` in the env) then
  `cmake --build --preset android-arm64 --target dol`: builds; ring and blake3 build scripts ran; `libdol.so` exports
  10 `pbnet_*` symbols incl. `pbnet_android_init_tls`.
- Configuring with `CC_aarch64_linux_android` set in the env drops only that pair from the cargo env line.
- `tools/check.sh`: passes (desktop build is unaffected: the block is `ANDROID`-only).

## Not verified

- `android-x86_64`, armeabi-v7a and x86 ABIs: not built (only arm64 was run; the wrapper names follow the same NDK
  pattern, `armv7a-linux-androideabi28-clang` and `x86_64-linux-android28-clang` both exist in NDK 28.2's bin).
- Windows-hosted NDK (`.cmd` wrapper path): no Windows host here.
- CMake's built-in Android support (no NDK toolchain file) path for the API level: the presets always use the NDK's
  toolchain file, so not exercised.
- Gradle/APK build and running on a device: no usable JDK for Gradle (only 27), no device, no disc image.
