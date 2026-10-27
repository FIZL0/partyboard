# Android Shell

This directory contains a minimal SDLActivity-based Android app wrapper for Party Board.

## Prerequisites

- Android SDK installed (`ANDROID_HOME`)
- Android NDK version used by CMake presets (`ANDROID_NDK_VERSION`)
- JDK 17+

Example:

```bash
export ANDROID_HOME="$HOME/Android/Sdk"
export ANDROID_NDK_VERSION="29.0.14206865"
export JAVA_HOME="/usr/lib/jvm/java-17-openjdk"
```

## One Step: Gradle Builds Everything

`./gradlew :app:assembleDebug` (from `platforms/android`) runs the CMake preset for each ABI first (game, aurora,
and the `pbnet` netplay crate through cargo, see `cmake/pbnet.cmake`), stages the `.so` files into
`app/src/main/jniLibs`, then packages the APK. Needs `cmake`, `ninja`, `cargo` and the Rust target
(`rustup target add aarch64-linux-android`) on `PATH`. `ANDROID_HOME` / `ANDROID_NDK_VERSION` default to the SDK
Gradle uses (`local.properties` `sdk.dir`) and its newest NDK.

- `-Ppartyboard.nativeAbis=arm64-v8a,x86_64`: ABIs to build (default `arm64-v8a`; `x86_64` needs
  `rustup target add x86_64-linux-android`, and is what the emulator runs).
- `-Ppartyboard.skipNative=true`: package whatever is already staged (the manual steps below).
- `-Ppartyboard.cmake=/path/to/cmake`: a cmake that is not on `PATH`.

Gradle 8.13 needs a JDK 17 to 24 (`JAVA_HOME`); newer JDKs fail with "Unsupported class file major version".

## Build Native Libraries

```bash
cmake --preset android-arm64
cmake --build --preset android-arm64

cmake --preset android-x86_64
cmake --build --preset android-x86_64
```

These builds produce:

- `build/android-arm64/Binaries/libmain.so`
- `build/android-x86_64/Binaries/libmain.so`

## Stage Libraries Into APK Project

```bash
./android/scripts/stage-jni-libs.sh
```

This copies:

- `libmain.so` -> `android/app/src/main/jniLibs/arm64-v8a/`
- `libmain.so` -> `android/app/src/main/jniLibs/x86_64/`

## Refresh SDL Java Shim (Optional)

If you update SDL and want to refresh the embedded Java shim files:

```bash
./android/scripts/sync-sdl-java.sh
```

## Build APK

```bash
cd android
./gradlew :app:assembleDebug
```

Output APK:

- `android/app/build/outputs/apk/debug/app-debug.apk`

## Launch With Runtime Args (adb)

You can pass command-line args through the activity intent:

# TODO
```bash
adb shell am start -n com.mariopartyrd.partyboard/.PartyBoardActivity \
  --es partyboard_args "'/sdcard/Download/GMPE01_00.iso'"
```

Supported extras:

- `partyboard_args`: single shell-like argument string
- `partyboard_argv`: string-array argv
- `partyboard_disc`: compatibility shortcut (single ISO path)
