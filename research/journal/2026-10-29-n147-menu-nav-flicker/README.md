# N147: pad/keyboard menu navigation flickers

User (2026-10-29, after 0.1.3): "the controller moving around the menu looks bad, try and make it not flicker when
selecting things".

## How it was measured

`harness/harness.diff` (not committed; apply it, build, run `harness/run.sh`) adds `src/port/ui/n147_harness.cpp`,
called at the start of `ui::update()`:

- `PB_PAD=key@frame,...` does what a pad press does in input.cpp: `ProcessMouseLeave`, key down, key up a frame later.
- `PB_TRACE=a-b` logs every frame: the focused element, then every visible button / tab / select-button with its
  pseudo classes (F focus, V focus-visible, H hover, S selected) and its computed background, border alpha, colour,
  opacity, transform, decorator and box-shadow.
- `PB_SHOTS=a-b,...` takes `aurora_capture_frame_ui` every frame in the ranges.
- `PB_MOUSE=x;y@f` or `=Label@f` moves the mouse (onto a button's centre).
- `PB_FAKE_CONTINUE=n` adds n fake Continue entries (N146's hook).
- `SIZE=852x393` in run.sh resizes the window with Hyprland (iPhone 15 Pro landscape, as N141).

`harness/onehl.py out.log a-b` counts the highlighted items in each traced frame. An item is highlighted when its
look differs from its resting look (its most common look while not focus-visible or hovered, worked out separately
for :selected, which is the open tab or the chosen value). It reports every frame that does not show exactly one
highlighted item, or where that item is not the focused or hovered one.

## What flickered: two separate causes

### 1. Every move was a 0.12 s cross-fade (home screen, Settings, tabs)

RmlUi takes a transition from the element's *new* style. `#menu-list button.anim-done` (home) and
`button` / `select-button` / `tab` (windows) had their transition on the base rule. So the button losing focus faded
out over the same frames that the button gaining it faded in.

On the home screen that gave ~6 frames per move with two half-lit buttons. Both had grey text (#9c9c9c on mid blue)
because the text colour fades white→dark, and `font-effect: shadow` → `none` switches at once. The transform also
went 1.013 / 1.007 between them. Trace before the fix (`home1`, 4 moves): 60 of 131 frames did not have exactly one
highlighted button.

In Settings, `box-shadow` (the focus ring) is not transitioned, so the ring jumped while the background faded.
Before: 136 of 306 frames.

### 2. Settings/Online rows lost their ring and background a few frames after a move (render bug)

The frame captures showed something the computed values did not. Example: "Graphics Backend" focused, final colours
computed. It showed its ring for ~5 frames after the move (while the background transition ran), then **no ring and
no background at all**. The row before it also showed nothing, and so did the window panel: the title art showed
through it.

The cause, in order:

- With a box-shadow, RmlUi bakes background + border + shadow into one texture. The texture is made through a
  CallbackTexture (`GeometryBoxShadow::GenerateTexture` → `SaveLayerAsTexture`) and cached for as long as the look is
  unchanged (`BoxShadowCache`).
- aurora's `SaveLayerAsTexture` fills that texture with a queued `TextureCopy` frame op.
- Fast Menus (N131, on by default) skips presenting every other game frame while bootDll runs. bootDll is held
  behind the launcher, so this covers the whole home screen and Settings. A skipped frame (`FramePacket::skipDraws`)
  runs only its texture uploads: `encode_op` drops its render passes **and its TextureCopy ops**.
- RmlUi still updated and rendered on skipped frames. So a box-shadow texture made on one stayed blank for as long
  as it was cached.
- During a transition the look changes every frame, so a new texture is made every frame, and the frames that are
  shown made their own. When the look settles, whichever frame made the last texture decides: on a skipped frame the
  item is invisible until its look changes again. Hence the ring that appears and then goes away.

Dawn validation (skip_validation commented out, temporarily) reported nothing: the copies are simply never run.

## Fix

- `cmake/patches/aurora/0016-rmlui-skip-render-on-skipped-frames.patch`: `rmlui::record_frame` still runs
  `Context::Update` on a skipped frame (animations and events go on) but returns before `Context::Render`. Nothing of
  that frame is shown, so callback textures are made on the next shown frame, where their copies run.
  `gfx::frame_draws_skipped()` reads the frame being recorded. It also saves the UI's draw work on skipped frames
  (Fast Menus, --turbo's throttle). All patches reverse-check, and the set applies to a clean aurora HEAD in glob
  order.
- `res/rml/prelaunch.rcss`: `#menu-list button.anim-done` has `transition: none`. That also overrides the intro's
  `.delay-N`; without it a focus move would animate the transform with up to 0.7 s of delay. The 0.12 s transition
  moved to `:hover`.
- `res/rml/window.rcss` (button, select-button) and `res/rml/tabbing.rcss` (tab, close): the transition moved from
  the base rule to `:hover`.

What this means for each input:
- Pad/keyboard: the highlight moves in one step, in the frame of the key press.
- Mouse: hovering in still fades (0.12 s home, 0.1 s windows; traced: Quit/Settings fade in over ~6 frames).
  Hovering out now snaps instead of fading.
- Touch: taps go through the same hover/active states as the mouse.

A mouse resting over one button while the pad focuses another shows two highlights (hover and focus), as before.
The next pad press clears hover (input.cpp's `ProcessMouseLeave`).

## Results (after the fix, all with Fast Menus on, desktop 1280x960 unless noted)

| run | what | onehl.py |
| --- | --- | --- |
| home2 / home3 | home, 1 Continue: down x3, up; home3 also mouse on Quit, then on Settings | 0 / 131 frames bad. home3: 0 bad in the pad window. The only 2-highlight frames are mouse-hover + focus, as before |
| set2 | Settings: 4 row moves on Prelaunch, next/prior/next/next tabs, 2 Video button moves | 0 / 326 |
| onl1 / onl2 | Online window: Host buttons, Join (Code field, Paste Code), LAN tab | 0 / 321, 0 / 221 |
| phone1 | 852x393, 3 Continue (list scrolls): down x4, up x2 | 0 / 211 |
| phone2 | 852x393, Settings rows + Video tab | 0 / 161 |

Frame captures (looked at as contact sheets, every frame around each move: set2 rows 429-442, tabs 519-524 and
649-656, Video 559-564 and 589-592, home3 299-302/329-332/389-392, onl2 599-601/719-721/759-761, phone1 729-765,
phone2 829-863): one highlighted item in every frame, changing in the frame of the press, and rings and row
backgrounds stay drawn. The shots are not committed (disc art).

On the phone-size home screen the list's smooth scroll-to-centre (`Component::focus`, `ScrollBehavior::Smooth`)
keeps moving the list for a few frames after the highlight has moved. That is the list sliding, not a second
highlight, and I left it alone.

## Not verified

- Modals (`button.modal-btn`): no capture. They use the same `button` rules in window.rcss that are fixed and
  checked here (Settings Video buttons, Online buttons). I found no quick way to open one from the harness.
- Real devices (iPhone, Android) and the browser build: checked only on desktop Linux, at desktop and iPhone landscape
  sizes. The aurora patch is not platform specific, but no web or mobile build was run. Portrait phone size was not
  run.
- A real game controller: the harness sends the key events that input.cpp's pad mapping sends
  (`ProcessMouseLeave` + key down/up). SDL's gamepad path itself was not driven.
- Mouse hover-out now snaps instead of fading. That is intended, but nobody has looked at it by eye.
- Shown frames vs. captures: `aurora_capture_frame_ui` was taken on every game frame. With Fast Menus some of those
  frames are not presented, so the captures cover more frames than the screen shows.
