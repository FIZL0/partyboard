# N61: keycap button icons for keyboard ports

Under Auto, a port played on the keyboard used to show Xbox glyphs (Auto's fallback). Now it shows keycaps with the
bound key's name, like opentennis's keycaps: a light cap on a darker lip, dark letters.

## What I did (src/port/button_icons.cpp, include/port/button_icons.h)

- New family `PB_FAMILY_KEYBOARD` (before `PB_FAMILY_COUNT`): display name "Keyboard", and "keyboard" for
  `PB_ICON_FAMILY`. `auto_family()` picks it when the port has no SDL controller (`PADGetIndexForPort < 0`) and its
  keyboard bindings are active (`PADGetKeyButtonBindings != nullptr`, the same test controller_config uses). A port
  with neither still falls back to Xbox. As with Touch (N58), only Auto changes. An explicit Button Icons setting
  still wins, and the setting got no "Keyboard" choice.
- Glyph ids: keycaps are port-independent, so a glyph id encodes the key itself. `0x1000 + 16 + scancode` is one
  key (aurora's mouse "scancodes" are -2..-6, and -1 is unbound). `0x2000 + n` is a cluster of four keys (up, down,
  left, right) drawn as an inverted T. n indexes a process-wide table (`clusters()`, under a mutex, at most
  0x1000 entries). New `pb_button_icons_glyph_is_key()`. `pb_button_icons_glyph_for_entry` and
  `pb_button_icons_glyph_rgba` handle these ids, so the RmlUi strip/preview (button_glyphs.cpp, whose range check now
  accepts key ids) gets keycaps too.
- What each message icon shows on the keyboard (`key_glyph_id`):
  - A/B/X/Y/Z/START: the key bound to that button (`PADGetKeyButtonBindings`).
  - L/R: the button binding, or else the analog `PAD_AXIS_TRIGGER_L/R` binding.
  - Stick: a cluster of the LEFT_Y_POS (up), Y_NEG, X_NEG, X_POS key axis bindings. C stick: the RIGHT_* bindings.
    D-pad: a cluster of the UP/DOWN/LEFT/RIGHT button bindings.
  - An unbound input shows a "?" cap. An unbound cluster cell is an empty cap.
- Labels (`key_label`): SDL's scancode names, as the controller config lists them (not the layout-aware key name,
  so the icon matches the config screen). Upper-cased and cut to 4 characters the font can draw. Some names are
  shortened by hand: ENT, ESC, BKSP, TAB, SPC, SHFT, CTRL, ALT, META, CAPS, INS, DEL, PGUP, PGDN, LMB/MMB/RMB/M4/M5,
  KP<x> for the keypad. Arrow keys draw stroke arrows. Letter size and stroke shrink with the label's length. A
  cluster cell shows one character (an arrow, the key, or the keypad digit).
- Stroke font: the rest of A-Z and 0-9, plus `? . , ; : ' \` / \ [ ] = *` and four arrows (`\1`..`\4`). Existing
  letters are untouched, so the other families' art is byte-for-byte the same. `#` and other symbols outside the font
  are dropped from labels (SDL's non-US `#` key shows "?").
- `make_glyph` now takes a canvas. Keycap glyphs have their own cache (`key_glyph`, a map by id) for the in-game
  ANIMDATA.
- `pb_button_icons_dump_sheet`: the new last row (Keyboard) shows sample keycaps instead of the per-control glyphs.

Art: `sheet.png` (every family. The last row is Keyboard: A Z M W 0 5 9 ENT SPC SHFT ESC TAB F12 KP8 / up left
LMB ?, a WASD cluster and an arrow cluster) and `font.png` (each of A-Z, 0-9 and the punctuation keys as a cap,
from `harness/font.c`). Build `font.c` like N49's sheet.c, against libdol.so.

## Checks (disc GMPE01, `mash-a.script --turbo`, own XDG_DATA_HOME/HOME per run)

- Keyboard bindings for port 0 come from `harness/keyboard_bindings.py`, which writes
  `$XDG_DATA_HOME/MarioPartyRD/Party Board/keyboard_bindings.dat` (format v3: 32-byte header, then per port a bool,
  12 x {s32 scancode, u16 button}, 10 x {s32, u16 axis, s16}). Bindings: A=J, B=K, X=U, Y=I, START=Return, Z=Q,
  L=1, R=E, d-pad arrows, stick WASD.
- **Script runs never load keyboard_bindings.dat.** aurora loads it lazily in its own `PADRead`, and under `--script`
  `netplay_pad_read` returns the script's pads without calling it. For the check I added one temporary line in
  `netplay_pad_read` that calls `local_pad_read` into a throwaway buffer. It was reverted and is not committed. Real
  play calls `PADRead` every frame, so the bindings are loaded before any icon draws.
- My first try failed twice. The harness was missing, and then I moved the file into `$XDG_DATA_HOME/Party Board`
  (only the shader caches go there). config.json and the bindings are under `MarioPartyRD/Party Board`.
- Real Auto path with bindings loaded: PB_ICON_LOG shows `Keyboard glyph 41xx/8192` for every icon, with caps J
  (A), K (B), U (X), I (Y), Q (Z), E (R), ENT (START) and the WASD cluster (stick). PB_ICON_SHOT frames show them in
  the original boxes: modesel (WASD Select, J Choose, K Return to Title), board turn menu (J Dice, Q View Map,
  E Zoom Out) and instDll (ENT Start, Q Practice, E Next). Shots are not committed (disc art).
- Game state: `kb2` (keycaps) and `kbx` (same bindings and harness, `PB_ICON_FAMILY=xbox`) give byte-identical
  17400-frame hash logs, ending in `AE36457C2B93C6C0` (N60's hash).
- One keyboard run (`kb`) took another path at hash line 1123 and crashed at frame 10608 in m425 `fn_1_E914` <-
  `fn_1_101C4` <- Hu3DDrawPost (coredumpctl). That is the known N63 crash, and the divergence is N53's run-to-run
  nondeterminism: the repeat run with the same setup finished with the usual hash.
- Unexplained, harness-only: with the harness line but no bindings file, two runs (Auto and `PB_ICON_FAMILY=xbox`)
  both ended on `EB35849B2E8FF23C`, not `AE36...`. Either it's nondeterminism, or the extra aurora `PADRead` with no
  controller changes something. The harness line is not committed, and committed code never calls PADRead under
  `--script`, so I didn't chase it.

## Not verified

- A real keyboard on a real Auto session without a script: the binding lookup was driven through the file and the
  harness. With no script the bindings come from aurora's normal `PADRead` load, so this path is only partly covered.
- Size at 1x: frames are 2048x1536 (3.2x). The WASD cluster's letters are readable there. At a 640x480 window each
  cell letter is about 3 px tall, which is probably not readable. Single keycaps should be fine.
- The RmlUi settings preview strip and the controller config with the Keyboard family: not opened. They use
  `pb_button_icons_glyph_for_entry` / `glyph_rgba`, which the dump and the in-game run cover. The controller
  config's keyboard binding rows still list key names as text (no keycap images). The task didn't ask for that.
- The L/R press loop (icons 7/9), the C stick and d-pad clusters, and mouse buttons in game: mash-a's path has no
  such prompts. They were checked only in sheet.png.
- Keyboard ports 1-3 and two keyboard players: not run.
- Android/iOS with a hardware keyboard: no device run.
