# N130: Screen safe zone (Safe Area setting)

User (2026-10-28): "have a screen safezone for mobile and such like opentennis". Ported from opentennis
`crates/hst-base/src/widescreen.rs` (B314/B590/B608/M106) and `android.rs` `safe_area`.

## What it does

- **Settings > Video > Safe Area** (`video.safeArea`, int): `Auto` (0, default) = the device's safe-area insets;
  `1 %`..`10 %` = that margin on every side, or the device's inset where it is bigger (opentennis `insets()`);
  `Off` (-1) = the whole window, device insets ignored.
- **The game picture is fitted inside the safe rect.** In opentennis the 3D picture bleeds under the notch and only the
  HUD/menus are inset. Party Board can't do that: the board HUD is drawn by the game into the same picture, so the
  whole picture (and the RmlUi layer, which aurora composites into the same rect) moves inside. The rest of the window
  is black. With Lock 4:3 the 4:3 rect is fitted inside the safe rect; without it the frame buffer takes the safe
  rect's shape (as it takes the window's today), so nothing is letterboxed twice.
- **aurora** (new patch `cmake/patches/aurora/0014-present-safe-insets.patch`): `aurora::window::set_present_insets(l,t,r,b)`
  (shares of the window, clamped 0..0.45) and `present_rect()`. `get_window_size()` sizes the frame buffer from the
  safe rect, and `webgpu::calculate_present_viewport()` fits the content inside it. Every user of that function
  (present pass, RmlUi render target size, RmlUi mouse/touch mapping, IME rect) follows, so input maps correctly.
- **Port** (`src/port/ui/ui.cpp`): `update_safe_area()` runs at the top of every `ui::update()`. It reads the device
  insets every 30 frames, applies the setting (`safe_area_shares`), and pushes the result to aurora only when it
  changes. Device insets come from:
  - `PB_SAFE=l,t,r,b`: window (logical) px, any platform, for desktop shots (opentennis `HST_SAFE`);
  - Android: the new `PartyBoardActivity.safeInsets()` (JNI): root window insets of `systemBars | displayCutout`, in
    physical px. Not SDL's `SDL_GetWindowSafeArea`, because SDL's Android insets also include
    `systemGestures`/`mandatorySystemGestures` (the back-gesture strips on both sides of a gesture-nav phone). Those
    would shrink the picture for no reason. SDL's value is still the fallback if the JNI call fails;
  - browser: `env(safe-area-inset-*)` read from a hidden probe element (`pb-safe-probe`; the page already has
    `viewport-fit=cover` and a full-page canvas);
  - iOS and the rest: `SDL_GetWindowSafeArea` (UIKit `safeAreaInsets` on iOS).
- `safe_area_insets(context)` (RmlUi windows, menu bar, touch pad padding) now returns the part of the safe area that
  still lies inside the RmlUi context. The context *is* the fitted picture, so that is 0 in practice (sub-pixel
  rounding is dropped). At Off it is 0 too, so menus reach the edges as in opentennis.

## A deadlock this exposed (fixed in 0014)

On Android at Auto, the game hung on a black screen at boot (overlay 1). `debuggerd -b`:
`SDLThread` was in `window::resize_swapchain` (holding the SurfaceLock, the activity mutex) →
`webgpu::resize_swapchain` → `gfx::gpu_synchronize` → `render_worker::synchronize()`, waiting for a queued frame
whose present needs the same lock. Meanwhile the UI thread was blocked in `onNativeInsetsChanged` on that mutex. It is the
same pattern as N123/patch 0013. It shows up whenever the frame buffer size changes while a frame is queued. Before
N130 that rarely happened on Android (the fb only followed the window). Fix: `webgpu::resize_swapchain` waits through
0013's `synchronize_surface_unlocked()`. After the fix, Auto boots and runs on the emulator.

## Checks done

- Desktop, `PB_SAFE=80,30,80,50` (window 1280x960, scale 1.6): fb 1792x1408; whole-window captures (harness below)
  show 128/48/128/80 px black strips, with the launcher, the toasts and the Settings window inside. A `mash-a.script`
  party run (frames 3000-10500: party mode menu, board intro, board HUD with 4 player panels, minigame intro) has every
  HUD panel inside the safe rect.
- Variants: Off + PB_SAFE → fb 2048x1536 (whole window); 5 % margin, no PB_SAFE → 5 % on every side (fb 1844x1382);
  Lock 4:3 + PB_SAFE → fb 1792x1344, centred in the safe rect; no PB_SAFE at Auto → unchanged (fb 2048x1536, no
  insets). Desktop without PB_SAFE pushes no insets, so the picture is unchanged from before.
- Settings > Video shows the "Safe Area" row (value "3 %" from a config with `video.safeArea: 3`).
- Browser (`tools/web.sh` WEB_FAST, `tools/web_check.mjs`, headless Chromium): with a `WEB_PRELOAD` that makes the
  probe's computed padding 48/0/48/21 px, the console logs `safe area 0.0375 0 0.0375 0.0257` and the shot shows the
  title screen with 48 px side strips and a 21 px bottom strip.
- Android (emulator hst34, x86_64 APK, Android 14; it has a punch-hole cutout on the left in landscape):
  `safe area 0.0533 0 0 0` (128 px) → the picture starts at x=128, the title screen and a toast are drawn right of the
  cutout. After rotating to the other landscape: `0 0 0.0533 0`, picture 0..2272. Off → the whole screen.
- Builds: desktop (gate), web, arm64 + x86_64 APKs (`./gradlew :app:assembleDebug -Ppartyboard.nativeAbis=...`), iOS
  IPA (`tools/ios-ipa.sh` + `ios-ipa-check.py` ok).
- Patch chain: every patch still reverse-checks on the patched tree, and a fresh `git archive` of aurora HEAD with all
  patches applied in order is identical to the working tree.

## Harness

`harness/harness.diff` (not committed to the tree; `git apply` it, build, `git apply -R` after):
`PB_WINDOW_SHOT=1` makes each `aurora_capture_frame` also write `<path>.window.png`, the frame as presented (picture
+ RmlUi + black strips, surface size) via an offscreen copy pass with the present viewport. `PB_SHOT_AT=f1,f2` +
`PB_SHOT_DIR` shoot at those `ui::update` counts and log fb size and RmlUi insets; `PB_SETTINGS_TAB=<i>` opens
Settings at tab i on frame 300. `harness/run.sh <name> <secs> [config-json] -- args` runs one game in its own user
dir under `.n130-runs/`. Shots hold disc art: not committed.

## Not verified

- **A real iPhone / iPad** (none here): the IPA builds and passes the import check, but `SDL_GetWindowSafeArea`'s
  values on a notched iPhone, and whether the window reaches under the home indicator, were not seen.
- **A real Android phone with a notch / gesture nav**: only the emulator's punch-hole cutout was tested. The
  gesture-strip exclusion is reasoned from SDL's `onApplyWindowInsets` source, not observed.
- **Real browser insets** (`env(safe-area-inset-*)` on an iPhone Safari page): only faked through `getComputedStyle`
  in headless Chromium, which has no notch.
- **Web and iOS were not rebuilt after the last aurora change** (the `synchronize_surface_unlocked` line in
  `resize_swapchain`, which is plain shared C++ already used on both). The desktop gate and the Android APKs were
  rebuilt with it.
- **Touch-pad layout on a phone at Auto**: the pad now lives inside the fitted picture (it is RmlUi). No shot of it on the
  emulator (it appears on touch; no touch was sent).
- **Netplay**: the insets only change presentation sizes (fb/viewport), never game state, so hashes are unaffected
  by design. No two-peer run with different safe areas was done.
