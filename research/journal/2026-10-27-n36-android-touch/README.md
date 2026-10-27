# N36: Online menu by touch, checked by eye on Android (x86_64 emulator)

## Result

The online menu now works by touch on the emulator, checked by eye: menu bar → Online, all five tabs, the Code
field with the soft keyboard, a hosted lobby over the public relay, the character list, and a number field with the
number pad. Getting there took three fixes. Without them the APK drew nothing at all, and later crashed on the
first network use.

## What was wrong

1. **Every Android frame was black, and it wasn't the emulator.** N12 synced the SDL Java shim to upstream 3.4.10,
   and that dropped the three local `auroraNativeSetSurfaceReady(...)` calls in `SDLSurface.java` (added in
   b1c33527). aurora (`lib/window.cpp`) starts Android with `g_surfaceReady = false`, and only that JNI call sets it.
   So `is_presentable()` stayed false, and `aurora_begin_frame` returned false on every frame. The SDL thread
   spun at 100% CPU in `launchUILoop` (`debuggerd -b` backtrace), and SurfaceFlinger got zero buffers. N12 blamed the
   black `screencap` on headless capture. In fact the app had never presented a frame. The emulator window
   (in a private display) and `screencap` were both black for the same reason.
   Fix: the calls are back (`SDLSurface.java`). `platforms/android/scripts/sync-sdl-java.sh` now warns when a sync
   loses them.
   After the fix, plain `adb exec-out screencap -p` works with `-gpu host -feature Vulkan`. **No visible window or
   device is needed.**
2. **Any iroh endpoint bind crashed the app** (here: opening the LAN tab, which starts the LAN browse). The path is
   iroh `EndpointInner::bind` → netwatch → `netdev::os::android::interfaces` → `ndk_context::android_context()`, which
   panics "android context was not initialized". netdev wraps that panic in `catch_unwind`, but `net/` is built with
   `panic = "abort"`, so the process aborts. n0-dns-resolver reads the same context. Host, Join and LAN all bind, so
   no netplay could have worked on Android.
   Fix: `pbnet_android_init_tls` (already called at boot with SDL's env and activity) now also calls
   `ndk_context::initialize_android_context` with the VM and a leaked global ref to the activity, once per process
   (`init_ndk_context` in `net/src/android.rs`), whatever the TLS result. `ndk-context` (already in the lock) is
   now a direct Android dependency.
3. **The soft keyboard never went away** after Enter or after tapping outside a field. The edit was committed, but
   the keyboard kept covering the bottom half of the menu (Paste Code / Join Game). RmlUi's `WidgetTextInput` only
   ever sets `keyboard_showed` to false (constructor), so `SetKeyboardActive(false)` returns early, and
   `SystemInterface::DeactivateKeyboard` is never called. Fix: `BaseStringButton::stop_editing` calls
   `Rml::GetSystemInterface()->DeactivateKeyboard()` (→ aurora → `SDL_StopTextInput`) after removing the input.
   The next edit starts text input again through `ActivateKeyboard` as before.

Also: aurora's log went to stdout/stderr only, which go nowhere on Android. `aurora_log_callback`
(`src/port/portmain.cpp`) now also writes to logcat on Android (tag `PartyBoard`). That is how (1) and (2) were found.

## How it was run

- APK: `./gradlew :app:assembleDebug -Ppartyboard.nativeAbis=x86_64` (JDK 21 Temurin in the scratchpad), installed
  with `adb install -r`.
- Emulator: `emulator -avd hst34 -gpu host -feature Vulkan -read-only -no-snapshot -no-audio`. It ran windowed inside
  a private `gamescope --backend headless` + `Xwayland :93`, so nothing was on the user's screen. After fix 1 the
  window wasn't needed, because `screencap` works.
- Disc: `$PB_DISC` copied into the app's files dir with `run-as` (`cat /sdcard/... | run-as <pkg> sh -c 'cat > files/...'`).
  `files/config.json` has `backend.isoPath` set to it and `backend.wasPresetChosen: true`. (While still hunting for
  the black screen I also tried `"backend.graphicsBackend": "opengles"`. Dawn has no GLES adapter there, so aurora
  falls back to Vulkan, which works: the emulator passes the host's RADV through.)
- Touch: `adb shell input tap` for one finger. `input` can't do multi-touch, so the 3-finger menu tap is raw
  `sendevent` MT events on `/dev/input/event2` after `adb root` (`tap3.sh`). `drive.sh` holds the helpers and
  coordinates (landscape 2400x1080).

## Checked by eye (screenshots here, 960x432)

- `01-before-black.jpg`: before fix 1, black (the same with the window or `screencap`).
- Prelaunch → Play by tap; the game boots to the title.
- `02-three-finger-menu.jpg`: the 3-finger tap opens the menu bar (Settings / Online / Achievements / Quit).
- Tap ONLINE: the Online window opens. Tap HOST, JOIN, LAN, CONTINUE, PLAYERS: each tab switches and its pane
  renders ("No games found yet", "No saved online matches", the player names / relay list).
- `03-code-soft-keyboard.jpg`: a tap on CODE brings up the QWERTY keyboard (`dumpsys input_method`
  `mInputShown=true`). The panel moves up, so the field stays visible above the keyboard. Typing by tapping keys gave
  "Abcd": the first letter is capitalized (sentence caps, as N12 predicted). No autocorrect suggestions came up
  with the AOSP keyboard.
- `04-enter-hides-keyboard.jpg`: Enter commits "Abcd" and hides the keyboard (`mInputShown=false`; before fix 3 it
  stayed `true`). Tapping the field again opens the input with the full value selected. Tapping the help pane
  commits and hides the keyboard.
- `05-lan-no-crash.jpg`: the LAN tab after fix 2 (before it, a SIGABRT tombstone with the netdev backtrace).
- Players tab: a vertical swipe scrolls the pane to NETWORK / RELAY / OWN RELAY URL.
- `06-hosted-relay-code.jpg`: Host Game from the phone, over the public relay. It shows the lobby with short code
  A26C, Copy Long Code, and seats (1P Player 1 - Mario, 2P Open). A swipe scrolls to YOUR CHARACTERS / MATCH /
  Start Match. Tapping a seat lists Open / CPU levels in the right pane.
- `07-character-list.jpg`: a tap on "PLAYER 1 Mario" lists the characters in the right pane. A tap on Yoshi selects
  it, and the row reads "PLAYER 1 Yoshi". A swipe scrolls the list down to Daisy and Waluigi.
- `08-turns-number-pad.jpg`: in the RULES tab (Board, Turns, Bonus Stars, Team Battle, Handicaps), a tap on TURNS
  opens the number pad. Typing 30 and Enter sets 30 and hides the pad.
- Desktop, for the shared `string_button.cpp` change: Linux build in `Xwayland :93`, F1 → Online → Join. I clicked
  CODE, typed "wxyz" and pressed Return, then clicked again and typed "q": "wxyzq". Text entry still starts again
  after the new `DeactivateKeyboard`.
- `tools/check.sh` passes.

## Minor, seen but left alone

- A swipe that starts on a list entry leaves a focus outline on that entry (Peach, after scrolling the character
  list). It doesn't select it. Cosmetic.
- Sentence caps on the Code field (pbnet upper-cases codes) and the keyboard's "Suggest contact names?" strip: the
  aurora `InputType` work is N31's.

## Not verified

- **A real phone / arm64**: no device attached and no arm64 image. Only the x86_64 emulator with host Vulkan ran.
  Only the x86_64 APK was rebuilt this time.
- **Joining from the phone and a match from the phone**: there was no second peer. The phone hosted on the public
  relay (code shown), but nobody joined, and Start Match was not pressed. The bind and relay path ran without a
  crash, but lockstep on Android was not exercised.
- **The platform TLS verifier with the new ndk-context init**: short-code creation ran (the A26C code came back
  from the relay), but I didn't check in logcat whether the platform verifier or the bundled roots were used.
- **The SDL surface lifecycle beyond start-up**: backgrounding and resuming (surfaceDestroyed → surfaceChanged)
  was not tried. The restored calls are the same ones b1c33527 shipped.
- **Paste Code and Copy Long Code by touch** (the Android clipboard): not tapped.
- **iOS**: none of this was looked at there. The SDL/aurora surface path differs, and there is no Xcode here.
  The RmlUi keyboard fix is shared code, so it applies to iOS too, untested.
