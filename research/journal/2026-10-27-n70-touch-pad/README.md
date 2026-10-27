# N70: on-screen game pad for touch screens

## Result

A translucent GameCube-style pad is drawn over the game on touch screens: a stick (bottom left), A/B/X/Y (bottom
right, GameCube layout: big green A, red B down-left, X right, Y above), L and R (top corners), Z (under R) and
START (top centre). Its touches play controller port 1.

- **Shown** from the first finger down (a real touch, not one SDL makes from the mouse), **hidden** again by any
  controller button, a controller stick or trigger past half travel, or a key press (not key repeat, not the
  phone's volume keys). It also hides while any RmlUi menu is open: menus take touches themselves (N36) and block
  pad input anyway.
- **Input** goes through aurora's existing virtual pad, `PADSetVirtualStatus(PAD_CHAN0, ...)`. aurora's `PADRead`
  merges it into port 1's status: buttons OR'd with the real pad's, the larger stick deflection wins, nothing while
  `PADBlockInput` is on. That needs no change in `src/game`, `src/REL` or `netplay.c`: online, netplay reads the
  local pads with the same (real) `PADRead`, so the touches are this machine's first seat, and they go through
  lockstep like a pad's.
- A tap that goes down and up within one frame's events still reaches the game for one frame (latched until the
  next `ui_update`, after the frame's `PADRead`).
- The stick floats: a touch on the base moves the knob from the base's centre; a touch elsewhere in the left half
  (below the L button row) moves the base under the finger for that touch. Deflection is linear to the base's
  radius (±127, the game's `PADClamp` cuts it to GameCube range), 12% dead zone. Sliding a finger from one button
  onto another presses the new one (A to B without lifting).
- **Settings > Input > Touch Controls** (`game.touchControls`, default on) turns it off.
- The "No controller assigned" toast is not shown while the pad is up (it plays port 1).
- `include/port/touch_pad.h` exposes `touch_pad_shown()` and `touch_pad_port()` for N58 (touch family of button
  icons: when the pad is shown, port `touch_pad_port()`'s prompts should show the touch buttons).

Desktop: a pad that was never touched never calls `PADSetVirtualStatus`, so a machine without a touch screen plays
exactly as before.

## Files

- `src/port/ui/touch_pad.{hpp,cpp}`: layout, hit testing, finger tracking, the virtual pad status, and the drawing
  (a passive RmlUi document like the Overlay, pushed under it in `portmain.cpp`). Layout is computed from the RmlUi
  context's size and the window's safe-area insets, with the shorter side as the unit, so it scales with the
  screen; SDL's normalized finger positions map to the same space (as in `ui/input.cpp`).
- `ui/ui.cpp`: `handle_event` passes events to `touch_pad::handle_event` first.
- `settings.{h,cpp}`, `ui/settings.cpp`: the setting and its row.
- `ui/overlay.cpp`: no controller warning while the pad is shown.

## How it was checked

Temporary harness (`harness/`, applied as `harness.diff` + `touch_test.inc`, reverted): `PB_TOUCH_TEST=<file>`
pushes SDL finger/key events (`SDL_PushEvent`, so through aurora's event loop like real ones) at UI frames, and logs
`HuPadBtn[0]` / `HuPadStkX[0]` / `HuPadStkY[0]` (what the game read) whenever they change. `$PB_DISC`, fresh user dir
under `build/n70run/a` with `skipBootSequence` + `skipPreLaunchUI`, the game at modesel. Window 1280x960 (context
2048x1536 at density 1.6).

`harness/touch.txt`:

```
frame 300 push down 1 0.150 0.800                     (on the stick's centre)
frame 301 shown 1 live 1 HuPadBtn 0000 stick 0,0
frame 310 push move 1 0.250 0.800                     (128 px right, 0.69 of the radius)
frame 312 shown 1 live 1 HuPadBtn 0000 stick 63,0     (after PADClamp)
frame 400 push move 1 0.150 0.650                     (straight up, past the radius)
frame 402 shown 1 live 1 HuPadBtn 0000 stick 0,72     (full up)
frame 420 push up 1
frame 422 shown 1 live 1 HuPadBtn 0000 stick 0,0
frame 500 push down 2 0.805 0.750 / push up 2         (A, down and up in the same frame)
frame 502 shown 1 live 1 HuPadBtn 0100                (A for exactly one frame)
frame 503 shown 1 live 1 HuPadBtn 0000
frame 700 push down 3 0.500 0.085                     (START, held 60 frames)
frame 702 ... HuPadBtn 1000
frame 762 ... HuPadBtn 0000
frame 900 push key                                    (a key press)
frame 901 shown 0 live 0                              (pad hidden)
```

Before the first touch nothing is set (`shown 0`, no virtual status).

Screenshots (gamescope headless, invisible):

- `01-before-warning-fix.jpg`: stick held up-right, X held (lighter). The "No controller assigned" toast still
  covered the pad, so it is now suppressed while the pad is shown.
- `02-stick-held.jpg`: stick held right (`hold2.txt`), no toast. The knob is where the finger is (checked by
  cropping: centre 280,767 = the touch at 0.22,0.80).
- `03-menu-hides-pad.jpg`: a synthetic 3-finger tap opens the menu bar and the pad disappears; an A touch while the
  menu is open does not reach the game (`HuPadBtn` stays 0).

**Android** (x86_64 emulator, AVD hst34, `-gpu host -feature Vulkan -no-window`, APK from
`./gradlew :app:assembleDebug -Ppartyboard.nativeAbis=x86_64` with a scratch JDK 21, disc in `files/` via `run-as`
as in the N36 journal, `skipPreLaunchUI` + `skipBootSequence`). Real touches through `adb shell input`:

- `04-android-stick.jpg`: during a 6 s `input swipe` from the stick's base up-right, the pad is drawn (inset from the
  left/right by the safe area, the emulator's cutout), the knob follows the finger, and there is no controller toast.
- `05-android-a-chose-party-mode.jpg`: after the stick moved the card cursor to Party Mode, an `input tap` on A
  advanced the message ("In this mode, you can play on any map you want." with the Select/Choose/Return legend).
- `06-android-key-hides-pad.jpg`: `input keyevent KEYCODE_SPACE` hides the pad, and the "No controller assigned"
  toast comes back (port 1 has no pad again).

`tools/check.sh` passes.

## Not verified

- **Online play with the pad** (lockstep): the touches go in through the same real `PADRead` that netplay reads
  local pads from (`local_pad_read` → `__real_PADRead`, which merges aurora's virtual status), so nothing new is on
  that path, but no two-peer match was played by touch. Needs the N38 two-peer harness with a touch harness on one
  peer.
- **Multitouch on Android** (stick and a button at once): `adb shell input` is one finger. The desktop harness held
  two fingers at once (stick + X, `01-before-warning-fix.jpg`), and the code tracks fingers by id; the raw
  `sendevent` MT route (N36's `tap3.sh`) would check it on the emulator.
- **iOS**: no Apple toolchain here (never built). The code is plain SDL3 finger events + RmlUi, nothing per-platform.
- **Real phones / tablets**: sizes and the feel of the stick (dead zone 12%, linear) only judged from screenshots.
  Portrait screens get the same layout scaled to the short side; not looked at.
- **The S1d guest page**: does not exist yet.
- **The Settings > Input > Touch Controls row**: built, not opened in a screenshot; the setting itself was
  exercised only at its default (on).
