# N72: button glyph contrast on dark backgrounds

## Result

N49's dark-bodied glyphs now have a light ring between their dark outline and their fill, so their shape reads on
dark backgrounds (modesel's navy, the board's black prompt bar) the way the bright GameCube sprites do. Generic's
face cluster draws the three other buttons as light rings with a dark centre, plus the filled white "this button"
dot. Before, those three were dark dots that disappeared on navy and left only the white dot.

Change (`src/port/button_icons.cpp`):

- New `kEdge` (light grey 0.84/0.85/0.88) and `kInkW` (0.022 glyph units, about 1 px of the 48-px bitmap).
- `disc()` and `pill()` (every round face button, bumper/trigger pill, menu button, the stick's base and guide)
  now paint ink, then `kEdge` inset by `kInkW`, then the fill inset by 0.06 as before. The outer ~1 px stays dark, so
  the glyph keeps an edge on light backgrounds. About 2 px of `kEdge` sits inside it.
- The d-pad cross gets the same ring.
- Generic face cluster: as described above. The ring's size and the dot positions are unchanged.
- Not changed: keycaps (already light), touch glyphs (already rimmed, N58), Generic's bumper/trigger wedges and the
  stick's inner knob (filled light grey already). Fills (`kBody`) are kept dark, so the white and coloured letters
  keep their contrast.

## Checks

- Sheet (N49's `sheet.c` against the built `libdol.so`, composited by `sheet_bgs.py` on blue, navy, near-black and
  light backgrounds): `sheet-before.png` vs `sheet-after.png`. Rows: PlayStation, Xbox, Switch, Generic, Steam Deck,
  Touch, Keyboard. Before, on navy/black, the bodies of the circles, pills and d-pads blended into the background
  and Generic's unpressed dots were invisible. After, every glyph has a visible outline on all four backgrounds.
  On the light one it still has its dark edge.
- In-game, GMPE01 Rev 1 (`$PB_DISC`). N60's run: `PB_ICON_FAMILY=<generic|xbox|gamecube> PB_ICON_SHOT=...
  --script tools/netplay/mash-a.script --turbo`, fresh user dirs, in parallel. I cropped the modesel prompt
  (ovl 74, "Select / Choose / Back") and the board turn menu (ovl 89, Dice/Item/View Map/Zoom Out) and checked them
  by eye. Generic's four dots now read on modesel's navy and on the board's black bar. Xbox's A/B discs show their
  edge on the black bar. RB/RT have a clear outline over the board terrain. The crops are not committed (disc art;
  rerun to regenerate).
- The change is visual only: all three runs' 17400-frame hash logs end in `AE36457C2B93C6C0`, the same as N60.
- `tools/check.sh` passes.

## Tried / failed

- First wait loop used `pgrep -f "partyboard --script"`, which matched its own shell command line and never ended.
  I killed it and checked the runs directly.

## Not verified

- instDll (ovl 3) and resultDll (ovl 84) shots were taken but not looked at closely. Their backgrounds are lighter
  windows, and the glyphs keep their dark outer edge there.
- PlayStation, Switch and Steam Deck were not shot in game, only on the sheet. They use the same `disc`/`pill` code.
- RmlUi glyph images (N59's `src/port/ui/button_glyphs.cpp`) take the same bitmaps (`pb_button_icons_glyph_rgba`),
  so they get the ring too. Those menus were not run or captured (N60's capture is pre-RmlUi).
- Touch's on-screen pad (N58) uses its own `touch_glyph` and is unchanged.
- No real controller or other display; same limits as N49/N60.
