# N58: touch button icons (blocked on N69)

## What I found
- N58 is conditional: "when an on-screen pad exists, add a touch family to
  src/port/button_icons.cpp that draws its buttons".
- No on-screen game pad exists on main (dab086b7). Touch input is handled only in
  src/port/ui/input.cpp, which turns finger events into RmlUi menu clicks/taps (N36).
  `grep -ri "on-screen\|virtual pad\|FINGER"` over src/ finds nothing else.
- No task in plan/TODO.md builds one (S1d only says "touch controls on").

## Decision
- A touch family has nothing to draw until the pad exists: its glyphs must match that
  pad's button shapes, labels and colours, which nobody has designed yet. Inventing
  them here would mean designing the pad inside an icon task (out of scope: "do N58
  and nothing else") and would likely have to be redone.
- Filed **N69** (on-screen game pad overlay for touch) and blocked N58 `--after N69`.

## Hook points for whoever picks N58 up after N69
- src/port/button_icons.cpp: add `PB_FAMILY_TOUCH` to `PbIconFamily` (before
  `PB_FAMILY_COUNT`; the glyph cache is sized by it), a name in `forced_family()`'s
  `kNames` ("touch") so `PB_ICON_FAMILY=touch` can test it, and cases in
  `face_letters`, `shoulder`, `stick`, `menu_button` / `draw` that copy the pad's art.
- `auto_family(port)`: return the touch family when the port's last input came from
  the on-screen pad (N69 should expose that, e.g. a `pb_touch_pad_active(port)`).
- The RmlUi menus share these icons, so they follow automatically.

## Not verified
- Everything: no code changed, so no in-game check was run. Gate not relevant beyond
  `done`'s own run.
