# N60: screenshots of N49's button glyphs on the disc

## Result

N49's glyphs now have in-game screenshots, taken from GMPE01 Rev 1 (`$PB_DISC`). They sit where the original GameCube
sprites sit and are about the same size. Every pair of frames (a family against GameCube) is pixel-identical except
inside the icon rectangles, so the swap changes nothing else on screen. One look problem turned up (contrast on dark
backgrounds, below). It is filed as a follow-up and not fixed here.

## The capture path (new, committed)

AGENTS.md rules out gamescope/Xwayland capture, so the frame is read back inside the game:

- `cmake/patches/aurora/0003-gfx-frame-capture.patch` (new aurora patch, applied at configure like 0001/0002) adds
  `aurora_capture_frame(const char *path, uint32_t delay_frames)` to `aurora/gfx.h`. `aurora_end_frame` counts the
  delay down. On the due frame the render worker copies `present_source()` (the EFB as presented, MSAA-resolved,
  before RmlUi/ImGui) to a MapRead buffer in the same command encoder. After the submit it maps the buffer, and the
  map callback writes an RGB PNG with libpng, which aurora already links. Handles BGRA8/RGBA8 (sRGB too); other
  formats log an error. The PNG has the EFB size: 2048x1536 here, 3.2x the 640x480 game frame.
- Test hook in `src/port/button_icons.cpp`: `PB_ICON_SHOT=<dir>`. The first time an overlay draws a given message
  icon (GameCube included, so the originals can be shot too), it queues captures 20, 60 and 120 frames later as
  `<dir>/ovlNNN-iconNN-dNNN.png`. `PB_ICON_SHOT_DELAYS=0,8,200` overrides the delays. Without the env var the hook
  does nothing.
- Capturing doesn't touch game state. A `mash-a.script` run without the hook and all seven runs with it give
  byte-identical 17400-frame hash logs (final `AE36457C2B93C6C0`; this differs from N49's 004C982C415C8583 because
  N64 changed the hash since).

Run used (one per family, fresh user dirs, in parallel):

    PB_ICON_FAMILY=<f> PB_ICON_LOG=1 PB_ICON_SHOT=$R/<f>/shots XDG_DATA_HOME=$R/<f>/xdg HOME=$R/<f> \
      timeout -s KILL 150 build/linux-default-relwithdebinfo/partyboard \
      --script tools/netplay/mash-a.script --hash-log $R/<f>/h.log --turbo

`<f>` = gamecube (originals), xbox, playstation, switch, generic. `tools/` has the comparison scripts:
`contact.py` (thumbnail grid), `crops.py` (the same region of each frame, stacked), `diffbox.py` (bounding box of
pixels that differ between two frames, per region).

## What the screenshots show

Screens with a glyph on screen in at least one capture: modesel file select (ovl 74: stick, A, B), mentDll party
setup (70: stick, A, B), board turn menu (89: A Dice, B Item, Z View Map, R Zoom Out; then stick Select / A Choose),
instDll (3: START, Z Practice, R Next; only at d120, the box is still opening before that) and resultDll (84: A Next,
only at d0/d8 and d300).

- **Place and size**: per icon, the diff box of GameCube against Xbox/PlayStation/Switch is exactly the GameCube
  sprite's box (board A 81x69, B 71x69, Z 100x61, R 79x64; instDll START 71x64, Z 99x61, R 78x62, in 2048x1536
  pixels). So these glyphs fit inside the original footprint. Generic's face cluster pokes out by 2-4 px (A 85x72,
  B 75x72), which is not visible by eye. Label text doesn't move (the whole-frame diff box covers only the icon
  column).
- **Centring**: by eye the glyph centres line up with the originals and with the text baseline, on the board and in
  instDll (which draws the icons smaller and scales the glyphs with them).
- **Look**: the round face glyphs (A/B, cross/circle, Switch B/A) match the originals' size. The bumper glyphs
  (RB/R1/R) are short wide pills, lower than GC Z's 30-px-wide slab but readable. The triggers (RT/R2/ZR) and the
  Start glyphs (Xbox menu, PS options, Switch +, Generic play) are fine. PS Options is a narrow upright pill, the
  least recognisable at instDll's smaller size.
- **Problem (follow-up N72)**: the glyphs are dark-filled with a light rim, while the originals are bright. On the
  dark navy of modesel and the dark board bars they show less contrast. Generic's face cluster is the worst case:
  its three dark dots nearly vanish on modesel's navy, and only the white "this button" dot stays visible, so the
  prompt reads as a lone white dot.

## Tried / failed

- First parallel run: all four aborted at boot with "cannot open script": `$PWD` in each subshell was already the
  run dir. The script path was fixed and they were rerun.
- The default delays (20/60/120) missed resultDll's prompt (never on screen in those frames) and caught instDll only
  at 120. A second pair of runs (gamecube, xbox) with `PB_ICON_SHOT_DELAYS=0,8,200,300,450,700` got resultDll.

## Not verified

- **L/R press animation** (icons 7/9, bank 1 loop): no prompt in mash-a's path uses them (only 0, 2, 3, 6, 11, 12
  were drawn), so the animated banks were never on screen.
- **Steam Deck family** was not shot (not one of the runs. Its glyphs share the Xbox layout code, per N49's sheet).
- **Real controllers / remaps / keyboard / touch**: as in N49, families are forced with `PB_ICON_FAMILY`. No
  hardware pad was used.
- **Screenshots are not committed**: they show disc art, including the original icon sprites, and AGENTS.md forbids
  committing extracted assets. They were in the session scratchpad only. Rerun the command above to regenerate them.
- **RmlUi glyphs (N59)** are not in these frames: the capture is the game's EFB before RmlUi composites. A capture of
  the final composited image would need the copy pass rendered to an offscreen target. Not done.
- **Other platforms / backends**: Linux Vulkan only. Formats other than BGRA8/RGBA8 (for example a 10-bit surface)
  log an error and write nothing. Not hit here.
- Other minigames' own HUD button sprites (outside the message icons) were not surveyed; same as N49.
