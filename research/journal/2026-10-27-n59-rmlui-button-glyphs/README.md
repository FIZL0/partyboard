# N59: button glyph images in RmlUi

## Result

Our RmlUi menus now draw N49's button glyphs instead of only naming the family:

- **Controller config > Buttons / Triggers / Sticks**: each binding row shows the glyph of the native control it is
  bound to, after the value ("Z ... RB [RB glyph]"). The glyph follows the bindings and the port's family under
  Settings > Input > Button Icons. It is hidden while the row is waiting for a new binding, for keyboard ports
  (N57 is keycaps), for unbound rows, and for a GameCube family.
- **Controller config > Button Icons** (right pane): a list of what each of the game's prompts shows for that port
  (A, B, X, Y, Z, Start, L, R, Control Stick, C Stick, D-Pad, each with its glyph), or "GameCube: the game's own
  icons."
- **Settings > Input > Button Icons**: the per-port lines ("Port 1: Xbox") now carry the same glyphs in a row.
- **Menu hint**: the "Press F1 or [controller] Back to open menu" toast shows the back button's glyph for port 1's
  pad before its name. Not for a GameCube pad (its "R + Start" chord has no single glyph) or with no pad.

## How

- `include/port/button_icons.h` gets a single-glyph API: `pb_button_icons_glyph_rgba(family, glyph, out)`
  (48x48 straight-alpha RGBA, the same `draw()` as the game sprites and the sheet dump),
  `pb_button_icons_glyph_for_native_button/axis` (aurora mapping → glyph id), `pb_button_icons_glyph_for_entry`
  (what message icon 0-12 shows for a port, through `bound_ctl`, i.e. the same answer as the in-game swap), and
  `PB_GLYPH_SIZE`, `PB_GLYPH_NONE`, `PB_GLYPH_BACK`. The glyph id is the existing `Ctl` enum.
- `src/port/ui/button_glyphs.{hpp,cpp}`: registers an aurora runtime texture provider (already upstream in aurora:
  `aurora::rmlui::register_texture_provider`, used by `WebGPURenderInterface::LoadTexture`) for the scheme
  `pbicon://<family>/<glyph>`. Registered on first use, so no init code changed. Each glyph is drawn once and kept
  in a map (the span must outlive the provider call, and a reload after a device loss asks again). RmlUi's
  `JoinPath` passes `scheme:/...` sources through unchanged (a `:` before the first `/` counts as absolute), so a
  plain `<img src="pbicon://2/0">` works from any document.
- `SelectButton::set_glyph(src)` adds an `img.glyph` after the value on first use; `ControlledSelectButton::Props`
  gained an optional `getGlyph` (last field, so existing designated initializers are unaffected), polled in
  `update()` like `getValue`.
- CSS: `img.glyph` (1.5em, middle-aligned) in `window.rcss` and `overlay.rcss`.

## Checked

- **Glyph pixels**: `glyphs.c` (libdol scratch link, see the N49 journal) compares every
  `pb_button_icons_glyph_rgba` glyph (5 families x 21 ids) with its cell of `pb_button_icons_dump_sheet`: all
  identical, so the RmlUi images are N49's `sheet.png`. GameCube and an out-of-range id return -1; south → 0,
  left trigger axis → 15, left X axis → 17, -1 → none.
- **In the game** (`$PB_DISC`, fresh user dirs under `build/n59run`, temporary hooks in `test-hooks.diff`, plus an
  `fprintf` in the provider, all reverted): an SDL virtual gamepad with a given VID/PID is attached and given port
  1, the menu toast is shown, Controller config opens on a chosen page, and at frame 200 every `<img>` in every
  document is logged with its src, intrinsic size (texture loaded) and box. Logs in `logs/`:
  - Xbox 360 (045e:028e), Buttons page: A/B/X/Y/Start → Xbox glyphs 0/1/2/3/6, Z → RB (10), D-pad rows → 11-14.
    All 77x77 intrinsic (48 px at the 1.6 dp ratio), 38.4 px boxes, visible. The provider was asked for
    `pbicon://2/4` (Back) when the toast was built.
  - PS4 (054c:09cc), Triggers page: Trigger L/R → PlayStation L2/R2 (15/16). The Digital L/R rows have no native
    binding on this pad, so no image (as intended).
  - Switch Pro (057e:2009), Sticks page: Control Stick rows → left stick (17), C Stick rows → right stick (18).
  - NSO GameCube (057e:2073): no glyph images anywhere; the preview says "GameCube: the game's own icons."
  - The preview RML for each family lists the expected ids (logged).
- Build and `tools/check.sh`.

## Tried / failed

- First in-game run with a 60 s limit never reached the frame-200 dump (the hidden window is slow); the later runs
  used 200-240 s and `--turbo`.
- `git commit -a` for the WIP checkpoint left the new files untracked; harmless, noted so nobody wonders.

## Not verified

- **No screenshot of the menus.** AGENTS.md rules out gamescope/Xwayland capture and the game window is on a hidden
  workspace. What is checked: the textures load (non-zero intrinsic size, provider called with the right ids) and
  the elements are laid out and visible, and the pixels equal N49's checked sheet. How the glyph sits next to the
  text (size, alignment, spacing) was not seen.
- **Settings > Input > Button Icons pane and the Button Icons right pane** were not opened in the game (they build on
  focus); their RML was logged from `button_glyph_preview_rml` and is the same `<img>` form that was checked in
  Controller config. The Settings pane's port lines (text and now glyphs) are built when the pane opens, so picking
  another family there does not refresh them until it is reopened (unchanged from N49).
- **Toast glyph on screen**: the provider was asked for the Back glyph, but the toast had closed by the frame-200
  dump, so its element was not logged. When the setting forces a family different from the pad's type, the glyph
  follows the setting while the text name ("View", "Share") follows the pad; not changed.
- **Real controllers**: only SDL virtual pads with real VID/PIDs. Hardware detection is as untested as in N49.
- **Device loss / texture reload** and **Android/iOS/Windows**: not run (no Apple/MSVC toolchains; Android not
  built for this).
- GameCube family has no RmlUi glyphs: the game's own icons are disc art (win/icon.anm), not ours to rasterise
  into the menus. Drawing GameCube-styled glyphs of our own would be a separate task.
