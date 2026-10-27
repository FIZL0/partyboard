# N58: touch button icons

First attempt (same day) was blocked: no on-screen pad existed. It was filed as N69; N70 built it
(src/port/ui/touch_pad.cpp, include/port/touch_pad.h with `touch_pad_shown()` / `touch_pad_port()`), so N58 reopened.

## What I did
- `PB_FAMILY_TOUCH` in include/port/button_icons.h (before `PB_FAMILY_COUNT`), names "touch" (`PB_ICON_FAMILY`)
  and "Touch" (display).
- `auto_family(port)`: Touch when `touch_pad_shown()` and `port == touch_pad_port()` (port 1). This applies only
  under Auto. An explicit Button Icons setting wins, as for every other family. I added no "Touch" choice to the
  setting: the brief only asks for the Auto rule.
- `family_ctl()`: with Touch, the glyph is aurora's *default* binding of the GameCube input (A south, B east, X west,
  Y north, L/R the triggers, Z the right shoulder, stick left stick), not the port's physical controller mapping. The
  pad's A is A no matter how a connected controller is remapped. Used in `pb_button_icon_for_window` and
  `pb_button_icons_glyph_for_entry` (RmlUi menus).
- `touch_glyph()`: art copied from the pad's look. Dark bodies with a light rim, A green, B red, Z purple, white
  labels. Round face buttons and stick base with knob. Wide rounded boxes for L, R, Z and START (new stroke letter
  'S'). Opaque, not translucent like the pad, so they stay readable over the game. Controls the pad lacks (d-pad,
  C stick, back, guide, left bumper, stick clicks) fall back to Generic's art.
- `sheet.png`: every family's glyphs (N49's sheet.c/sheet.py). The last row is Touch.

## Checks (disc GMPE01, `mash-a.script --turbo`, own XDG/HOME per run, in parallel)
- `PB_ICON_FAMILY=touch` vs `=xbox`: PB_ICON_LOG shows the same glyph ids per icon, family Touch. PB_ICON_SHOT
  frames show the touch glyphs inside the original boxes: modesel (L stick, A, B), board (A Dice, B Item, Z View
  Map, R Zoom) and ovl 3 (START, Z, R).
- Real Auto path: N70's harness (harness/touch_test.inc, two lines into touch_pad.cpp, reverted after) pushed one
  SDL finger down/up at (0.5, 0.4), which is on no control. Result: `touch_pad_shown` became 1 and stayed 1, and
  every icon logged `Touch`. Its shots are byte-identical to the forced-touch run's.
- Hash logs of all three runs (forced touch, real touch, xbox) are identical (`AE36457C2B93C6C0`, the same as N60's).
  Icons and a touch that hits no control don't change game state.

## Not verified
- On a real touch device or the Android emulator: I didn't rebuild the APK. The code path is the same libdol
  function, and the desktop harness drove the real `touch_pad_shown()`.
- Flip back to a controller family mid-window: a gamepad press hides the pad, so Auto should go back. The scripted
  pads don't make SDL gamepad events, so I didn't trigger this here.
- RmlUi menus (settings preview strip, controller config) with Touch: not looked at. They reuse
  `pb_button_icons_glyph_rgba` / `glyph_for_entry`, both covered by sheet.png and the logs. Controller config draws
  a physical controller's native buttons in Touch art by position while the pad is shown. That is acceptable, but
  untested.
- The L/R press loop (icons 7/9) with Touch: mash-a's path has no such prompt (same as N60).
- Contrast of the dark bodies on dark backgrounds (the N72 concern): the light rim is meant to handle it, but I only
  checked it on the board and modesel frames.
- Screenshots are not committed (disc art). sheet.png is our own art only.
