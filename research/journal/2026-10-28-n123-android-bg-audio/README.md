# N123: Android background audio on a runtime (the stuck pause was a deadlock)

N112's pause-on-background (`src/port/audio.cpp` `app_event_watch` + SDL's own `AAUDIO_PauseDevices`) was never seen
working: on N112's local merge with N110 the emulator's UI thread got stuck ("top resumed state loss timeout",
"Activity stop timeout", black game view). It was not an audio bug.

## Reproduced from main

x86_64 APK (`./gradlew :app:assembleDebug -Ppartyboard.nativeAbis=x86_64`, Temurin 21 in the scratchpad), AVD hst34,
`emulator -avd hst34 -gpu host -feature Vulkan -read-only -no-snapshot -no-window -audio none` (`-audio none`: a null
host backend, the guest still has its audio HAL, so AAudio opens and nothing reaches the user's speakers). Disc and
`files/config.json` as in N36. `harness/cycle.sh <outdir> <cycles>` does HOME → back, screen off → on, recents →
back, printing the game's AAudio player state from `dumpsys audio` after each step.

The first HOME worked (`Paused for the background`, `state:paused`; back: `Resumed in the foreground`,
`state:started`, game drawn). The second HOME hung: no more `partyboard::` lifecycle/audio lines, AAudio stayed
`started`, and system_server logged `Activity stop timeout` / `Activity pause timeout` / `top resumed state loss
timeout` (17 of them over 3 cycles). It's a race, so whether the first, second or later background hangs is luck.

`adb root; debuggerd -b <pid>` / `debuggerd -j <pid>` on the hung process:

- **UI thread** (`main`): `SDLSurface.surfaceDestroyed` → `SDLActivity.onNativeSurfaceDestroyed` → waiting for
  SDL's `Android_ActivityMutex`.
- **Game thread** (`SDLThread`, `game_main`): `port_lifecycle_hold` → `aurora_can_present` (holds `SurfaceLock` =
  `Android_LockActivityMutex`) → `webgpu::release_surface` → `render_worker::synchronize`, waiting for the render
  thread.
- **Render thread**: running the queued frame's `end_frame` lambda → `window::SurfaceLock::SurfaceLock`, waiting
  for the mutex the game thread holds.

Game ↔ render is the deadlock; the UI thread just queues behind it, which is what Android reports. Whenever
`surfaceDestroyed` flips `is_presentable()` while a frame is still queued on the render thread, the game thread's
next `aurora_can_present` (N110, patch 0010) waits for that frame under the lock that frame needs. Upstream aurora's
`begin_frame` has the same shape (`SurfaceLock`, then `release_surface()` / `refresh_surface(true)`, both of which
start with `gfx::gpu_synchronize()`); N110 made it far more likely by calling `can_present` every loop.

## Fix: aurora patch 0013-surface-sync-unlocked

- `window::SurfaceUnlock` (window.hpp/.cpp): a scope that gives up every `SurfaceLock` this thread holds (a
  thread_local depth counter; SDL's activity mutex is recursive) and takes them back at the end. A no-op where
  `SurfaceLock` is (all but Android) and on threads that hold none.
- `webgpu::release_surface` / `refresh_surface` wait for the render thread through `synchronize_surface_unlocked()`:
  not on the render thread itself (`synchronize` never waits there, and its `Present`-failed / `Lost` /
  `Outdated` paths call these under the lock, which must stay held), not when the worker is idle, otherwise under a
  `SurfaceUnlock`. After the wait the caller holds the lock again; `release_surface` then unconfigures the surface,
  `refresh_surface` re-reads `is_presentable()` itself.
- While unlocked, `surfaceDestroyed` can finish and release the `ANativeWindow`; the render thread's frame then sees
  `!is_presentable()` and doesn't acquire a texture, and Vulkan's Android surface holds its own window reference until
  `Unconfigure`/release.
- Hunks are only in `SurfaceLock`'s ctor/dtor, the class after it in window.hpp, and gpu.cpp's two surface functions,
  clear of 0010's and 0012's hunks. `harness/pcheck.sh` regenerates the patch from a snapshot and reverse-checks every
  patch on its own; all 13 also apply in order to a clean `extern/aurora` HEAD and give the same tree.

No change in `src/`: audio.cpp's watch was right; it just never ran because the game thread was stuck.

## Checks

- **Emulator, fixed APK, `cycle.sh` 5 cycles** (same process throughout, pid unchanged):
  every HOME and every screen-off → `state:paused`, every return → `state:started`; logcat 10× `Paused for the
  background`, 10× `Resumed in the foreground`, 0 activity stop/pause/resumed-state timeouts. After the 5th return
  screenshots taken 4 s apart differ (the attract loop runs; the game view is drawn, not black).
- **Before the fix, same harness**: hung at the second HOME (above).
- **Desktop unchanged**: `tools/netplay/mash-a.script` cut to `end 2400`, `--turbo --hash-log`, with and without
  `PB_LIFECYCLE_TEST="bg@400,fg@700,hide@1000,show@1100"`, fresh user dir each: `identical: 2400 frames`. (A first
  pair sharing one user dir split at frame 1122 on cfg/plr: the second run read the first one's memory card. Not this
  change.)
- `tools/check.sh` passes.

## Tried / notes

- Recents (`KEYCODE_APP_SWITCH`) only takes window focus on this launcher (no onPause/onStop while the overview is
  showing), so the game and its audio keep running there. That is SDL's behaviour on multi-window-capable Android
  (it pauses on onStop), unchanged here.
- The task named iOS too. iOS's SurfaceLock is a no-op, so this deadlock can't happen there.

## Not verified

- **iOS at runtime**: no device or simulator here (IPA not rebuilt; the patch only adds code under
  `SDL_PLATFORM_ANDROID` plus a thread_local counter).
- **A real Android phone / arm64**: no device; only the x86_64 emulator with host Vulkan. The arm64 APK wasn't
  rebuilt (same source, other ABI).
- **Audible output on the emulator**: host audio was `none`; only the AAudio player state (`dumpsys audio`) and the
  game's own log were checked, as in N112.
- **Background during a party / minigame or an online match on Android**: cycles ran on the boot/attract screens
  (no controller assigned in the emulator). The deadlock depends only on a frame being in flight, not the scene.
- **Browser / Windows builds**: not rebuilt. In the browser `is_worker_thread()` is always true, so the new helper
  returns before touching anything.
