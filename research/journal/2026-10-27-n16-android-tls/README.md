# N16: Android TLS for short codes

## What was done

`codes.rs` `relay_get` / `relay_put` go through reqwest + rustls. reqwest's default verifier is
`rustls-platform-verifier`, which on Android needs a JNI init (the VM, a `Context`, its class loader) and panics in
the request's task without it. Ported opentennis's `crates/hst-base/src/android.rs` `init_tls` / `tls` (B418/B454):

- `net/src/android.rs` (`cfg(target_os = "android")`):
  - `pbnet_android_init_tls(env, activity) -> int`: C ABI. opentennis gets the VM from android-activity
    (`ANDROID_APP`); we are an SDL app, so C passes `SDL_GetAndroidJNIEnv()` and `SDL_GetAndroidActivity()`, and Rust
    wraps the env with jni 0.22's `EnvUnowned::from_raw` + `with_env_no_catch` (no attach needed: SDL's thread is
    attached) and calls `rustls_platform_verifier::android::init_with_env`. The verifier keeps its own global refs and
    the VM, so C deletes the activity local ref right after. Returns 1 if on, 0 if not (null args, JNI error).
  - `tls()`: the `rustls::ClientConfig` for reqwest, ring provider, HTTP/1.1 ALPN; the platform verifier if the init
    worked, else `webpki-roots` (so a failed init still gives working, panic-free short codes).
- `codes.rs` `client()`: on Android `Client::builder().tls_backend_preconfigured(android::tls())`; elsewhere
  unchanged (`Client::new()`).
- `net/Cargo.toml`: Android-only deps `rustls-platform-verifier 0.7`, `jni 0.22` (as `jni22`, no default features),
  `webpki-roots 1` (all already in the lock via reqwest/iroh; `Cargo.lock` only gained edges).
- `include/port/pbnet.h`: prototype under `#ifdef __ANDROID__`. `src/port/portmain.cpp`: called once after the
  `pbnet` version log, under `#ifdef __ANDROID__` (warns if 0). Not in `src/game` / `src/REL`; desktop unchanged.
- APK (`platforms/android`): the verifier's Kotlin classes must be in the APK or `init_with_env` / verification fail
  with ClassNotFound. As the verifier's README: maven repo
  `https://github.com/rustls/rustls-platform-verifier/raw/maven-archive/android-release-support/maven/` in
  `settings.gradle` (restricted to group `org.rustls`, since repos are settings-only there), and
  `implementation "org.rustls:rustls-platform-verifier:<v>"` in `app/build.gradle` with `<v>` read from
  `net/Cargo.lock`'s `rustls-platform-verifier-android` (0.2.0 now) so the Java and Rust halves never differ.
  ProGuard keep rule for `org.rustls.platformverifier.**` (minify is off today, but JNI-only classes would be stripped).
- `AndroidManifest.xml`: added `android.permission.INTERNET` (the app had none: no netplay at all would work on
  Android without it) and `android:networkSecurityConfig="@xml/network_security_config"`. Both also come from the
  AAR's own manifest by merging (checked: opentennis's cached copy of the 0.2.0 AAR declares both, and ships
  `res/xml/network_security_config.xml`, which allows cleartext only to CAs' revocation-list hosts); written out so
  they don't depend on the merger.

## Checked

- `cargo check --target aarch64-linux-android` with NDK 28.2 clang as `CC`/linker: builds (N2 couldn't: no NDK then).
- `tools/check.sh`: passes (host tests, desktop build).
- Android CMake build (`android-arm64` preset, NDK 28.2): builds and links once the NDK compiler is in the env
  (`CC_aarch64_linux_android`, `AR_aarch64_linux_android`, `CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER`);
  `libdol.so` exports `pbnet_android_init_tls` and contains the verifier's `init_with_env`, and `portmain.cpp`'s
  Android block compiles.

## What failed along the way

- Gradle: `./gradlew :app:dependencies` stops at "Unsupported class file major version 71": the only JDK here is 27,
  too new for Gradle 8.13. So the Gradle resolution of the AAR was not run.

- Android CMake build without those env vars: `pbnet_cargo` fails in ring's build script (cc-rs can't find
  `aarch64-linux-android-clang`): `cmake/pbnet.cmake` doesn't hand the NDK toolchain to cargo. Present before N16 (it
  is any Android pbnet build, not the TLS); filed as N17 instead of fixing it here.

## Not verified

- On a device: no Android phone/emulator run (and no disc image to boot the game). So whether `init_with_env` succeeds
  with SDL's activity, and an actual HTTPS GET/PUT to `relay.pkarr.org` on Android, are unchecked.
- Gradle resolving `org.rustls:rustls-platform-verifier:0.2.0` from the GitHub maven branch and merging its manifest:
  not run (JDK 27 only, see above). The URL/coordinates are the ones the verifier's README and opentennis's
  `tools/apk.sh` use, and that AAR exists there (opentennis downloaded it to `~/.cache/opentennis-apk`).
- The Groovy `Cargo.lock` parse in `app/build.gradle`: not executed, same reason.
- iOS / other targets: untouched by this task (their reqwest default verifier needs no init).
