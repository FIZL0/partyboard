# N128: Android Wi-Fi multicast lock for LAN lobbies

Many Android phones' Wi-Fi drivers filter incoming multicast unless an app holds a `WifiManager.MulticastLock`, so
iroh's mDNS and `net/src/dnssd.rs` (both on 224.0.0.251:5353) could hear no LAN lobby on a phone.

## What changed

- `platforms/android/app/src/main/AndroidManifest.xml`: `CHANGE_WIFI_MULTICAST_STATE` (a normal permission, granted
  at install: `dumpsys package` shows `granted=true`).
- `net/src/android.rs`: `MulticastLock`, a refcounted guard. The first live guard takes the system lock over JNI
  (`getApplicationContext().getSystemService("wifi").createMulticastLock("partyboard-lan")`,
  `setReferenceCounted(false)`, `acquire()`). The last one to drop calls `release()`. The JNI calls come from
  tokio workers, so `pbnet_android_init_tls`'s `init_ndk_context` now also keeps the `JavaVM` and a global reference to
  the activity (`JVM`), and the calls go through `attach_current_thread`. Java exceptions (e.g. a missing permission)
  come back as errors: the code logs them and holds nothing, and LAN then works as well as the phone allows.
- `net/src/transport.rs`: on Android, a `Transport` with LAN discovery (`has_mdns`) holds a guard from bind until
  `close()` (or drop). That covers Browse, a hosted lobby and a join, which also resolves a bare ID by mDNS, and a
  join that takes over the Browse's endpoint keeps the same lock.

## What went wrong first

The first version released the lock only when the `Transport` was dropped. On the emulator the lock was still held
after Leave: `run_host` closes the endpoint, but its `Arc<Transport>` stays alive in `Shared.lan` and in `keep_code`
after that. So `Transport::close()` now releases the guard too.

## Checked (x86_64 emulator hst34, Android 14)

APK: `./gradlew :app:assembleDebug -Ppartyboard.nativeAbis=x86_64` with Temurin 21 in the scratchpad.
`aapt2 dump permissions` lists the new permission. The emulator needs an X display for its GL setup even with
`-no-window` (without one: "Failed to get EGL display / Could not start renderer"). It ran with `DISPLAY=:1`, the
rootless Xwayland of a private `gamescope --backend headless`, so nothing appeared on the user's screen. Navigation by
touch used N36's `tap3.sh` and its coordinates. After each step I read `dumpsys wifi` → "Multicast Locks held":

| step | held |
|---|---|
| app at the title, Online window on HOST | none |
| LAN tab (browse) | `Multicaster{partyboard-lan uid=10142}` (01-lan-browse-lock-held.jpg) |
| back to HOST tab (`pbnet_lan_stop`) | none |
| Host Game (lobby open, code SK28) | `Multicaster{partyboard-lan ...}` (02-hosting-lock-held.jpg) |
| close → Leave | none |

logcat `WifiService: acquireMulticastLock` / `releaseMulticastLock uid=10142` show one pair per browse and per lobby.
The crate also builds clean for `x86_64-linux-android` (`cargo build --release`, no warnings).

## Not verified

- **A desktop host listed on the emulator/phone** (the task's check): the emulator's network is user-mode NAT
  (SLIRP) and doesn't carry LAN multicast either way, so a desktop host can't be heard there with or without the
  lock. No real phone was attached. What I checked is that the lock is taken and released at the right moments, not
  that a filtering phone now hears hosts.
- **arm64 / real devices**: only the x86_64 APK was built and run.
- **Join path**: a join that takes over the Browse's endpoint, or a fresh join's bind, wasn't driven on the
  emulator. It uses the same `Transport` code path as host and browse.
- **Lock release during a match / process death**: during a started match the lock stays held, because the
  endpoint is still open. Android releases it when the process dies. Neither case was observed.
