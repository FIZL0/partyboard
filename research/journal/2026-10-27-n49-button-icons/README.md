# N49: per-controller button icons

## Result

The game's message icons now show the buttons of the controller each window's player is using. There is a new
setting, **Settings > Input > Button Icons** (`game.buttonIcons`, int): Auto (default), PlayStation, Xbox, Switch,
Generic, GameCube. Under Auto each port gets its own controller's family. The glyphs are drawn at runtime from
shapes and a small stroke font (`src/port/button_icons.cpp`), so all the art is ours (`sheet.png`: rows PlayStation,
Xbox, Switch, Generic, Steam Deck).

## Where the game draws the glyphs

- The message font has no button characters. Code `0x0E n` in message text draws `spcFontTbl[n-1]` as a sprite
  (`HuWinSpcFontEntry`, `src/game/window.c`). Banks 0-19 come from `win/icon.anm`. I dumped them once from the disc
  with a temporary hook (not committed): 0 control stick, 1 d-pad, 2 A, 3 B, 4 X, 5 Y, 6 R, 7 R (press loop),
  8 L, 9 L (press loop), 10 C stick, 11 Z (30 px wide), 12 Start. 13-16 are arrows, 17 star, 18 coin, 19 the
  advance arrow. Only 0-12 are swapped.
- instDll's instruction pages and the board prompts both go through this path. instDll also loads its own sprites
  (`DATADIR_INST` 15-18), placed as the name/start/practice rows behind windows whose text carries the icons (the
  run below shows START, Z and R swapped in ovl 3). I did not dump those sprites, so whether they contain button
  art of their own is unchecked (see Not verified).

## How it works

- Hook (`window.c`, `#ifdef TARGET_PC`): `HuWinSpcFontEntry` asks `pb_button_icon_for_window(entry,
  active_pad & ~player_disable, &anim, &bank)`. The window's port is the lowest pad bit left. The board sets
  `player_disable` to everyone except the current player's port (`BoardWinPlayerSet`), so board prompts follow the
  player whose turn it is. If no bit is left (a COM's turn), it uses port 0.
- Family (`pb_button_icons_family`): the setting, or for Auto: no controller on the port (keyboard or none) → Xbox.
  Steam Deck VID/PID 28de:1205 → Steam Deck. A GC adapter or NSO GameCube pad (057e:2073) → GameCube (original
  icons). Otherwise `PADGetControllerType` (the SDL gamepad type): PS3/4/5 → PlayStation, Switch Pro/Joy-Cons →
  Switch, anything else → Xbox. A GameCube result keeps the original sprite.
- Binding: through the same aurora mappings that the controller config edits (`PADGetButtonMappings` /
  `PADGetAxisMappings`), so a remapped button shows its real binding. A/B/X/Y/Z/Start come from the button
  mapping, stick and C stick from the axis mapping. L/R come from the trigger axes, or from a digital binding when
  there is no axis. Each glyph is named by position (SDL south/east/west/north). So a Switch pad's south button
  (GC A by default) shows "B", as it should.
- Glyphs: 48x48 RGBA8 (GX tiles) drawn at the original 24x24 size, so they stay sharp at 2x. One `ANIMDATA` per
  (family, control), built on first use and locked for the process (`useNum` starts at 1, so `HuSprKill` never
  frees it). Bank 1 copies the original L/R press loop (normal 32 frames, pressed = shifted and darker, 32 frames).
- Our RmlUi menus: the setting's pane lists the family each port gets, and Controller config gets a read-only
  **Button Icons** row per port. They show the family name, not the glyph images (N59).
- Test hooks: `PB_ICON_LOG=1` logs each swap once per overlay (`ovl`, icon, port, family, glyph = `Ctl` enum).
  `PB_ICON_FAMILY=<gamecube|playstation|xbox|switch|generic|steamdeck>` forces what Auto picks, for runs
  without that pad. `sheet.c` + `sheet.py` render every glyph to a PNG from the built `libdol.so`.

## Checked (GMPE01 Rev 1 = `$PB_DISC`, `tools/netplay/mash-a.script`, fresh user dirs, turbo patched in temporarily)

- Auto, no physical pad (scripted pads) → Xbox. Board (ovl 89): A, Z → RB, R → RT, stick. instDll (ovl 3):
  START → Menu, Z → RB, R → RT. Also modesel (74), mentDll (70) and resultDll (84). See `logs/auto2.txt`.
- Setting Generic → Generic glyphs at the same places (`logs/generic.txt`).
- Setting GameCube → no swaps logged in the full 17400-frame run (the original sprites stay).
- `PB_ICON_FAMILY=playstation` under Auto → PlayStation glyphs (`logs/ps.txt`, 2500 frames).
- The swap is visual only. The Auto, GameCube and Generic runs' 17400-frame hash logs are identical
  (`hashdiff.py`), and the final hash is 004C982C415C8583, the same as N43's.
- Glyph art checked by eye in `sheet.png`. I redrew the Generic bumper/trigger as wedges, taller on the outer
  side, because the first rounded-corner version did not read as left vs right.

## Tried / failed

- Running for the icons without turbo: about 60 fps with vsync off, and no icon had shown by frame 4000. I patched
  `disableFrameLimiter = TRUE` in `main.c` for the runs and reverted it (N50 is the real flag).
- The first sheet dump was an env hook inside the game, but it never ran because no icon had been drawn yet.
  Replaced it with `sheet.c`, which links `libdol.so` and calls `pb_button_icons_dump_sheet` directly.

## Not verified

- **No screenshots of the glyphs in the game.** Game windows open on a hidden workspace, and AGENTS.md rules out
  gamescope/Xwayland capture. The in-game check is the log (which glyph was put in which overlay) plus the
  rendered sheet. Size, centring and the press animation on screen are unchecked (N60).
- **No real non-GC controller here**: Xbox, PlayStation, Switch and Steam Deck detection by SDL type/VID-PID is
  untested on hardware. The scripted pads have no controller, so the runs covered Auto's fallback (Xbox), the
  forced setting and `PB_ICON_FAMILY`. A remapped button was not tried either: no pad has mappings to change.
- **Keyboard** shows Xbox glyphs (Auto's "else Xbox"), not keycaps (N57).
- **Touch**: the brief says touch shows the touch buttons, but the port has no on-screen game buttons yet. Touch only
  drives the RmlUi menus (N36). Not done (N58).
- **RmlUi glyph images**: our menus name the family. They don't draw the icons (N59).
- **Online**: on a peer, a remote player's port has no local controller, so it gets Auto's fallback. That is
  visual only (hash unchanged), but whether it is the best choice online was not looked at.
- Other HuSpr button art outside the message icons (for example any minigame's own HUD sprites with GC buttons)
  was not surveyed beyond instDll. Only the message-icon path is swapped.
- Android/iOS/Windows builds were not built (no toolchains here for Apple/MSVC; Android not tried).
