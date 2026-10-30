# N150: GameCube button pictures in minigames, menus and models

## Result

The GameCube button pictures that minigames and menus draw from their own data (sprites and 3D model textures, not the
message-font icons N49 already swaps) are now redrawn with the buttons of the player's controller: PlayStation, Xbox,
Switch, Generic, Steam Deck, touch, and keycaps for a keyboard port. The art is N49's glyphs, drawn into the same
place at the same size. With the GameCube family (setting or a GC pad) nothing changes. It is pictures only. The
game's data, heaps and timing are untouched (the redrawn art lives in our own memory).

New: `src/port/button_art.cpp`, `include/port/button_art.h`. Hooks: `HuSprAnimRead` (sprman.c), `HuSprDisp`
(sprput.c), HSF `BitmapLoad` (hsfload.c), and per-player seats in m405 and m432 (`#ifdef TARGET_PC`, one call each;
`espSprIDGet` in esprite.c gives an esprite's sprite id). bootDll's N106 test jump gets `PB_FCO_INST` (only in
`-DPB_FCO_BOOT` builds), and `PB_ART_SHOT` turns off N120's present skip like `PB_ICON_SHOT` (imgui.cpp).
`button_icons.cpp` can now draw a glyph at any size (`Canvas` has a size, `pb_button_icons_glyph_rgba_sized`) and
gives the glyph of a message-icon button for a family (`pb_button_icons_glyph_for_entry_family`).

## The sweep: every GameCube button picture on the disc

I dumped every image on the disc and looked at all of them (tools below). That is 140 `data/*.bin` archives, every
ANM sprite (1802 files) and every HSF model texture (1873 models with bitmaps), shown as contact sheets plus
upscaled sheets of the small sprites. The character archives (`mario*.bin` etc.: models and motions) were dumped but
not looked at. These are all the GameCube button pictures there are, apart from the message icons (`win` file 3,
N49):

| data | what | where it is drawn | seat |
|---|---|---|---|
| m405 68, 69, 70, 71 | A, B, L, R (24x24 RGB5A3) | m405 HUD prompts per player (`B + A`, `L/R`) | the player's |
| m432 35, 36 | B, L (the same texels as m405's) | m432 HUD prompts per player | the player's |
| m432 37, 38, 39 | R, Y, X | m432 HUD prompts per player | the player's |
| mgmode 24 (0x18) | panel `A START / B BACK` (CMPR) | mgmodeDll (datalist.c) | first human |
| mgmode 46 (0x2E) | panel `B BACK` | mgmodeDll | first human |
| mgmode 47 (0x2F) | panel `A ADD/DELETE / B BACK` | mgmodeDll | first human |
| mpex 30 (0x1E) | panel `START START / B BACK` (the grey GC Start button) | mpexDll (mpex.c:2214) | first human |
| gamemes 5 | banner `Practice [Z] Back` | minigame_seq.c's practice banner (every minigame's practice) | first human |
| option 49 | L, L selected, R, R selected (4 banks) | optionDll sound and record pages | first human |
| m418 18-21 (model textures) | B, A, X, Y stones | m418 (the button stones) | first human |
| m460 9, 10 (model textures) | L and R floor panels | m460 map.c | first human |
| inst 0-5 (model textures) | the `START` button top and the `Z` slab (one texture each, shared by all six stands) | instDll's instruction screen | first human |

Checked and left alone:

- `win` 4/5 look like boxed A/B buttons, but they are `WIN_CARDA_ANM`/`WIN_CARDB_ANM` (memory card slots A/B).
- Direction pictures with no button letters: a white `+` (m405 67, m426 31, m431 40, m414's circle with `+`),
  m409 43 (stick/d-pad direction rings), m405/m432 66 (arrows). Those are the same on every controller.
- The boards (w01-w06, w10, w20, w21, board, bpause, b*), result, mstory*, present, modesel, ment, E3setup, ztar,
  saf, staff, title, instfont, instpic, mgconst, gamemes' font, m300-m333: no button art. Their button prompts are
  message icons (N49).
- `option` model 1's round orange `M` and `B` textures (001h05/06) are not GameCube buttons: they have no button
  shape or colour, and there is no `M` button. I took them as the room's decoration and left them alone.

## How it works

- **Finding them**: a picture is known by the FNV-1a 64 hash of its texels (format, size and hash in the `kArt`
  table). A hash is computed only when a bitmap's kind, format and size match a table entry. That is cheap and
  doesn't depend on how an overlay reads its data (espEntry, HuSprAnimReadFile, a model).
- **Sprites**: `HuSprAnimRead` notes an anim that holds known art. `HuSprDisp` asks `pb_button_art_sprite_anim` for
  the anim to draw. The answer is a copy of the anim whose known bitmaps are redrawn (RGBA8, 2x the original) and whose
  patterns' layers are scaled to them. The anim the game holds is never changed. Swapping only at draw time lets one
  shared anim show each player's controller: m405/m432 read the prompt once and make one sprite per player. A sprite's
  seat comes from `pb_button_art_esp_seat` (m405 `unk108`, m432 `unk_188`). Otherwise it is the first human player in
  `GWPlayerCfg` (COM seats too). An anim freed with its memory number and its address reused is caught because the
  bitmap's data pointer no longer matches.
- **Models**: after `BitmapLoad`, a known texture's `HSFBITMAP` gets the redrawn RGBA8 data (with a mip chain, in
  case a material asks for mipmaps), for the first human player's controller. A model is loaded once, so it doesn't
  follow a controller change while it is loaded.
- **Redrawing** (`redraw`): the original is decoded (all GX formats the game uses, C4/C8 palettes included), each
  button's rectangle is cleared or filled in (a lone icon becomes transparent, a panel's button takes each row's
  colour beside it, a model face its border's average colour, the practice banner's checker is drawn again), then the
  glyph is drawn centred. Option's selected L/R frames are tinted pink like the originals. Results are cached by
  (picture, family, glyphs), so they are drawn once per controller kind.
- The glyph per button is `pb_button_icons_glyph_for_entry_family`, the same binding lookup as the message icons
  (a remapped button shows its binding, a keyboard port shows the key).

## Tools (this directory)

- `disc_data.py <disc.ciso> <out>`: every `data/*.bin` from the disc.
- `unpack.c` (links `libdol.so` for `HuDecodeData`): every file of an archive, decoded.
- `gxart.py <raw> <out> [archives]` (+ `gxart_lib.py`): every ANM bitmap and HSF texture as PNG, plus a contact
  sheet per archive.
- `arthash.py <raw> <archive>/<file>[:<bmp>]`: format, size and hash of bitmaps, for the `kArt` table.
- `artshot.sh <raw> <out.png>` (`artshot.c`, `artsheet.py`): every known picture, original and redrawn in each
  family, through `pb_button_art_test_render` in the built `libdol.so` (no game run).
- `run.sh` / `batch.sh` (+ `harness.diff`, the temporary `PB_N150_EVERY` frame capture and `PB_N150_KEYS` bindings): in-game shots. They use a desktop build with `-DPB_FCO_BOOT` (`build/n150-shot`) and bootDll's
  N106 jump (`PB_FCO_OVL`), with N150's `PB_FCO_INST=1` to show the minigame's instructions first. Each run sets
  `PB_ICON_FAMILY` and `PB_ART_SHOT`, a new test hook that captures frames 20/120/300 (`PB_ART_SHOT_DELAYS`) after an
  overlay first draws a known picture, the GameCube originals included.

No disc art is committed. The dumps and shots stay in the session scratchpad and `.n150-runs/`.

## Checked (GMPE01 Rev 1 = `$PB_DISC`)

- **Redrawn art, every picture and family** (`artshot.sh`, no game): all 21 known bitmaps redrawn as PlayStation,
  Xbox, Switch, Generic and Touch with aurora's default bindings. Checked by eye: the glyph is in the original
  button's place at its size. Panels keep their tan rows, the practice banner its checker, option's selected frames
  their pink, and the model faces take their plate colour. Two sizes were raised after the first sheet: the practice
  banner's Z slot and inst's Z slab drew the glyph too small (1.4x the slot's short side now).
- **In-game shots, before and after** (`batch.sh`, then reruns; GameCube, Xbox, PlayStation, keyboard; `.n150-runs/`,
  not committed):
  - m405 HUD (`B + A` per player): circle B / cross A, Xbox B/A, keycaps `Z + X`.
  - m418 stones: B/A/Y/X stones show Xbox B/A/Y/X (model textures, also on the curved X/Y stones).
  - m432 bubble prompt (`B`): Xbox B, PlayStation circle, keycap.
  - m460 doors: L/R plates show LT/RT, L2/R2, keycaps.
  - instDll (Manta Rings): the 3D START button and Z slab show Menu/RB, Options/R1 and ENT/E. The message icons
    beside them (N49) agree.
  - Practice banner (minigame_seq.c): `Practice [RB] Back`, `[R1]`, `[E]`.
  - mgmodeDll's minigame list: the `B BACK` panel shows Xbox B.
- **Keycaps**: `--script` runs load no key bindings (memory note), so the first keyboard runs showed `?` keycaps,
  the message icons too. `harness.diff`'s `PB_N150_KEYS=1` binds the web build's default keys on port 0 (X/Z/C/S =
  A/B/X/Y, Enter = Start, Q/W/E = L/R/Z). With it, the art shows `X`, `Z`, `ENT`, `E`, `W`.
- **Per seat**: m405 with seats 1 and 2 human (`HUMAN=6`) logs the art drawn for port 1 and port 2
  (`button_art: ovl 13 m405 A port 2 -> Xbox`). COM seats show the first human player's.
- **Pictures only**: every shot run's hash log (with `heap=`) is byte-identical between the GameCube family and
  Xbox/PlayStation/keyboard (m405, m418, m432, m460, instDll, practice: 18 pairs). A full `mash-a.script` party on the
  normal desktop build (17400 frames, through instDll's swapped models) gives identical logs with `PB_ICON_FAMILY`
  gamecube and xbox. The final hash is `560CC8F72BA3BC23`.
- **Browser build**: `WEB_FAST=1 tools/web.sh` builds. `web_check.mjs` with `mash-a.script --turbo` runs all
  17400 frames (boot, file select, party setup, board, instDll, m422, results, board) to the same final hash as the
  desktop, `560CC8F72BA3BC23`.
- `tools/check.sh` passes.

## Tried / failed

- First in-game run: the shot build aborted at "Error initializing window". The agent shell has no
  `WAYLAND_DISPLAY`, and `run.sh` now sets `wayland-1`.
- First m405 swap: the glyphs were drawn at 2x and cut to the top-left quarter. A pattern's layer gives its texture
  rectangle in the bitmap's pixels. The swapped anim now has its own patterns and layers, scaled to the redrawn bitmap.
- `PB_FCO_INST` first went straight to the minigame: instDll skips itself when flag 0/11 (explanations) is off. The
  jump sets it now.
- The menus (mgmodeDll, mpexDll, optionDll) are hard to reach with fixed pad scripts. An A-tap script reached
  mgmode's list (the `B BACK` panel) but not its other panels, mpex's extra-room game start, or the option pages
  with L/R.
- The first keyboard runs showed `?`: there were no key bindings in `--script` runs (see Checked).

## Not verified

- **In game: mgmode `A START / B BACK` and `A ADD/DELETE / B BACK` panels, mpex `START / B BACK`, option's L/R page
  buttons.** These screens are behind menu navigation my scripts did not reach. Their redrawn art is checked in
  `artshot.sh`'s sheet only, through the same code path (sprite swap at draw time, the same as the m405, practice and
  mgmode `B BACK` pictures that were shot in game). Shooting them would need a menu-driving script per screen.
- **Switch, Generic, Steam Deck and Touch families in game**: only in the `artshot.sh` sheet. The in-game runs were
  GameCube, Xbox, PlayStation and keyboard, as the brief asks.
- **Real controllers**: there are no pads here. Families were forced with `PB_ICON_FAMILY` (as in N49/N60), and a
  remapped button was not tried.
- **Models follow the controller they were loaded with**: an m418/m460/instDll model already loaded keeps its art
  if the player switches controllers mid-minigame. Sprites follow at once.
- **COM seats**: their sprites (m405/m432) show the first human player's family. Online, a remote human seat shows
  this peer's family for that port (Auto's fallback, as N49 noted for message icons). Pictures only, the hash is
  the same.
- **Other disc versions** (GMPJ01, GMPP01, GMPE01 Rev 0): the hashes come from GMPE01 Rev 1. A version whose
  pictures differ (the Japanese and PAL practice banners and menu panels have other text) keeps the GameCube art.
  The other discs in `orig/` were not dumped. Follow-up N151.
- **Character archives** (`mario*.bin` etc., models and motions) were dumped but not looked through: they are
  characters' bodies.
- **Android, iOS, Windows builds** were not built. The new code is plain C/C++, compiled in the same lists as
  `button_icons.cpp`.
- **Direction-only pictures** (white `+`, m409's direction rings, m405/m432 arrows) are left as they are. They are
  the same on every controller, so they show no GameCube button. I chose this rather than drawing a d-pad or stick
  glyph over them.
