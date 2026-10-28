# N111: touch pad haptics

The on-screen game pad (N70) vibrates: a click when one of its buttons goes down (touched, or slid onto from
another button), a lighter tick when its stick moves into a new one of 8 directions. Physical controllers' and
keyboards' presses never reach this code (only `touch_pad.cpp`'s finger handlers call it).

Setting: Settings > Input > **Vibrate on Touch Buttons** (`game.touchHaptics`, default on), right under Touch
Controls; greyed out while Touch Controls is off.

## What is where

- `include/port/touch_haptics.h`, `src/port/ui/touch_haptics.cpp`: `touch_haptic(TOUCH_HAPTIC_PRESS | _STICK)`,
  checks the setting, then the platform:
  - Android: JNI to `PartyBoardActivity.touchHaptic(int)` (static): `Vibrator` from `VIBRATOR_SERVICE`,
    `VibrationEffect.createPredefined(EFFECT_CLICK / EFFECT_TICK)` on API 29+, `createOneShot(20 / 8 ms)` on 26-28
    (minSdk 26). The class is taken from the activity object (`FindClass` on SDL's thread only sees the system class
    loader), method id cached. `android.permission.VIBRATE` was already in the manifest (SDL's). Proguard keep rule
    added though minify is off.
  - iOS: `src/port/ios/Haptics.m`, `UIImpactFeedbackGenerator` medium (press) / light (stick), `prepare` after each
    so the next one has no wake-up lag; main-thread only, so it hops with `dispatch_async` if called elsewhere (SDL's
    iOS loop is on the main thread, so it is a direct call).
  - Browser: `EM_JS` `navigator.vibrate(15 / 6 ms)` if it exists (Android Chrome/Firefox); iOS Safari has no
    `navigator.vibrate`, nothing happens.
  - Linux/Windows: nothing.
- `touch_pad.cpp`: press haptic in `finger_down` (buttons) and `finger_motion` (slide onto another button). Stick:
  `update_stick_direction` after each stick touch/motion. In a direction past 0.5 of the base radius, centred
  again under 0.35. A new direction only once the stick is 0.15 of a sector past the current one's edge, so a finger
  resting on a sector line doesn't buzz. Re-centring is silent.

## Why not SDL haptics

SDL3's Android haptic backend reaches the phone vibrator only as an amplitude one-shot (no predefined click/tick,
which are the short crisp effects that phones tune), and needs `SDL_INIT_HAPTIC` + opening the device. On iOS SDL
haptics is GameController rumble, not the Taptic Engine. aurora's `device_ios.mm` `AuroraDeviceHaptics`
(CoreHaptics) is the game's PAD rumble motor on port 1, a continuous effect, so it is the wrong thing for a UI
tap too. So each platform's own API is used, as the task allows.

## How it was checked

- **Desktop**: `tools/check.sh` passes. On Linux/Windows `touch_haptic` only reads the setting and returns; the
  only callers are the touch pad's finger handlers (`grep touch_haptic\(`: three, all in `touch_pad.cpp`), so
  controller/keyboard play is unchanged.
- **config.json**: desktop run (`build/n111run`, fresh user dir) with `{"game.touchHaptics": false, ...}` and no
  `wasPresetChosen`, which makes the game re-save config.json at boot: the saved file still has
  `"game.touchHaptics": false`. A default (true) is not written, like every other setting (only values set by the
  user are saved). The UI toggle is `config_bool_select`, which `setValue`s and `config::Save()`s like the
  Touch Controls row above it.
- **Android APK**: `./gradlew :app:assembleDebug` (arm64-v8a, then `-Ppartyboard.nativeAbis=x86_64`), scratch JDK 21.
  `dexdump`: `PartyBoardActivity.touchHaptic:(I)V` public static; `libdol.so` has `touch_haptic`. On the emulator
  (hst34, Android 14, `-no-window -gpu host -feature Vulkan -read-only`, disc via `run-as`, config with
  `skipBootSequence` + `skipPreLaunchUI`), at the card pick screen: `input tap` on X, a swipe of the stick right, a
  swipe up. `dumpsys vibrator_manager` "Previous vibrations for usage TOUCH", all `opPkg=com.mariopartyrd.partyboard`:
  one `Prebaked{effect=CLICK}` (X), then two `Prebaked{effect=TICK}` (one per direction), nothing for the first
  touch that only showed the pad. Then config.json with `"game.touchHaptics": false`, restart, same taps and
  swipe: no new vibrations (still 3). Android puts predefined effects under usage TOUCH, so the phone's own
  "Vibration & haptics > Touch feedback" setting also scales or mutes them.
- **iOS IPA**: `tools/ios-ipa.sh` builds (`Haptics.m` compiled as ObjC ARC into libdol, `_PartyBoard_iOS_ImpactHaptic`
  + `_OBJC_CLASS_$_UIImpactFeedbackGenerator` import from UIKit), `tools/ios-ipa-check.py` passes (every import
  resolves).
- **Browser**: `WEB_FAST=1 tools/web.sh`, then `tools/web_check.mjs` with `WEB_PRELOAD=harness/web_preload.js`: it
  wraps `navigator.vibrate` to log, and at 105 s (the title screen; headless Chromium is slow to boot) sends
  synthetic touch `PointerEvent`s to the canvas (SDL3's Emscripten backend reads pointer events with
  `pointerType: "touch"`, not TouchEvents; my first try with TouchEvents reached nothing). Console: empty touch
  (shows the pad) no vibration; START `vibrate 15` (and the game went from the title to file select,
  `web-pad-shown.jpg`); stick right `vibrate 6`, stick up `vibrate 6`.

## Not verified

- **A real phone's feel** (Android, iOS, Android Chrome): no device here. The emulator logs the vibration requests
  but has no motor; strength/length (CLICK/TICK, medium/light, 15/6 ms) are untuned guesses.
- **iOS at runtime**: the IPA was only built and link-checked, never run (no device, no simulator on Linux).
  UIImpactFeedbackGenerator on the main thread is from Apple's docs, not observed.
- **Android API 26-28 fallback** (`createOneShot`): the emulator is API 34, so only the `createPredefined` branch ran.
- **The settings row in the UI** (label, greyed out when Touch Controls is off): not looked at on screen. It uses the
  same `config_bool_select` as its neighbours; the persistence path was checked through config.json instead.
- **iOS Safari** has no `navigator.vibrate`; the guard is there, not run in Safari.
- **Sliding from one button onto another** (also a press haptic): not driven, `input` has no such gesture handy.
