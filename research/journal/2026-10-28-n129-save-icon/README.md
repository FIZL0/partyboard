# N129: party board save without the popup

User (2026-10-28): "can we not popup the saving screen after every turn, just have an icon in the top corner".

## What the popup was (not the brief's message)

The brief named message `MAKE_MESSID(0x24, 0x07)` in resultDll as "Saving...". It isn't. From the disc
(`mess/board_e.dat`, read with N69's `ciso_extract.py` and `harness/mess.py`), 0x24,0x07 is `Ⓐ Next`: the results
screen's prompt, a window centred at the bottom (`HuWinCreate(-10000, 400, ...)`, -10000 means centred). It stays.

The save screen after every turn is resultDll `SaveExec` (main.c), run after the results when the party saves
(`GWSaveModeGet() != 1 && SLSaveFlagGet()`):
1. wipe out (30 frames), `Hu3DAllKill`/`HuSprClose`/`HuSprInit`/`espInit`, a black screen with sprite
   `DATA_MAKE_NUM(DATADIR_WIN, 0x20)` (the "saving, don't touch the card" picture), wipe in (20);
2. `SLSaveBoard`/`SLSaveBoardStory`, `SLCommonSet`, `SLSave`. **SLSave** (src/game/saveload.c) opens popups
   itself: `SLCreateSaveWin` (message 16,68 `Slot A: Saving!`), and inside it `SLFileWrite` / `SLFileCreate` open
   a second one (16,11 `Please do not touch the Memory Card or turn off the power.`, wait for the text, and
   `HuPrcSleep(60)` in SLFileWrite), each animated in and out around the write;
3. wipe out (20), back to the board.

The board itself has no end-of-turn save that shows anything: the N47 local autosave (start of each player's turn,
`netplay_party_autosave`) writes a file and shows nothing. Story mode's per-turn save is the same `SaveExec`
(`SLSaveBoardStory` branch), so it is covered too. The other `SLSave` callers are menus and one-off saves (see
"Not done").

## What changed (all under TARGET_PC)

- `src/REL/resultDll/main.c`: when the party saves, `SaveExecQuiet()` runs **while the results are still shown**
  (the same save statements as SaveExec, with `SLSaveQuiet`), then the 60-frame wipe-out of a party that doesn't
  save, then the board. SaveExec itself is unchanged (GC build). Saving before the wipe, not after it, means
  SLSave's own card error windows (`SLMessOut`: no card, wrong card, ...) still show over the results instead of
  under a black wipe where nobody could answer them.
- `src/game/saveload.c`: `SLSaveQuiet()` = `SLSave()` with `saveWinQuiet` set and `port_save_icon_begin/end` around
  it. While set, `SLCreateSaveWin` returns -1 (so `SLKillSaveWin` does nothing), and `SLFileWrite`/`SLFileCreate`'s
  "do not touch" window (now `SLWriteWinCreate`/`SLWriteWinKill`, the same calls) and SLFileWrite's 60-frame sleep
  are skipped. Without the flag every other caller runs the same calls as before.
- `include/port/save_icon.h`, `src/port/ui/save_icon.{hpp,cpp}`: the icon's state. Shown while a save runs and for at
  least 1.5 s (wall clock) after it began. The game never waits on it.
- `src/port/ui/overlay.{hpp,cpp}`, `res/rml/overlay.rcss`: a `<saving>` element in the overlay document: 44dp rounded
  box in the toast colours with Material Symbols' save glyph (U+E161), fading in/out (0.25 s) and pulsing its colour
  while open. Top right, 16dp inside the safe area (`safe_area_insets`, as the Window and menu bar use); top left
  when the FPS counter is set to the top right corner.
- `PB_ICON_SHOT=<dir>` also shoots each save: `save-NN-dNNN.png` at 2, 30 and 600 drawn frames after the save began
  (`PB_SAVE_SHOT_DELAYS` to change).
- **aurora patch 0014-gfx-frame-capture-ui**: `aurora_capture_frame_ui(path, delay)` (aurora/frame_capture_ui.h).
  0003's `aurora_capture_frame` copies the EFB before RmlUi, so no RmlUi overlay had ever been seen in a test (N132's
  Not verified says so). The new capture copies RmlUi's render target (game image + RmlUi, no ImGui) in end_frame's
  present callback when RmlUi drew that frame, and maps each readback in the next frame's callback. New files plus one
  line each in aurora.cpp (include + encode), rmlui.cpp (`capture_texture`) and aurora_gx.cmake (RmlUi sources).
  Made from a snapshot of the patched tree: every patch still passes `git apply --reverse --check` on its own, and the
  whole series applies forward on a pristine `git archive` of the submodule (results byte-equal to extern/aurora).

## Tests

Disc GMPE01 Rev 1 (`$PB_DISC`), relwithdebinfo, `harness/run.sh` (user dir under build/n129/, vsync off).

1. **Turbo party, `mash-a.script --turbo`, PB_ICON_SHOT** (offline w01 party; the card is written by resultDll after
   the first minigame, as in N132): `save-00-d002/d030/d600.png` written (2048x1536, RmlUi's target size). d002/d030 are
   the RESULTS screen, not black, no "Saving" picture and no `Slot A: Saving!` window; d030 has the icon in the top
   right corner, fading in. d600 is already the board (turbo frames are faster than real time, so the 1.5 s minimum
   still held the icon there).
2. **The save is real.** That run's card (`MemoryCardA.USA.raw`) with `local/party.json` deleted, then
   `PB_CONTINUE=party`: "continue: party board 0, turn 1/20, card file 0", the same as N132 got through SaveExec.
3. **Real time (no turbo), first version** (only `Slot A: Saving!` suppressed): the shot at 60 frames showed
   `Please do not touch the Memory Card...` over the results. That window comes from SLFileWrite, so the quiet
   flag now covers it as well (see Tried).
4. **Real time, final**: shots at 2/15/30/45/60/75/90/120 frames after the save began. 2: results, icon fading
   in. 15-45: results with the icon top right (44dp box, save glyph), no window. 60: the white wipe (resultDll's
   `WipeColorSet(255,255,255)`), icon still there. 75: the board fading in, icon there. 90 and 120: the board
   ("YOSHI START!"), icon gone (the 1.5 s minimum; the quiet save itself takes no visible time). The same card
   check as test 2 on this run's card: "continue: party board 0, turn 1/20, card file 0".

## Netplay

Online matches never reach the save: `netplay_match.c` sets `GWSaveModeSet(1)` for the match (318/345), and the
online checkpoint (`netplay_checkpoint`, before the `if`) is untouched. So a lockstep/`--match` run takes the
"doesn't save" branch, whose code is not changed, and its hash logs can't differ between peers because of N129.

`tools/netplay/party_check.sh` (two `--match` w01 parties of this build, `--turbo`, same pads) with `PB_ICON_SHOT` set:
both runs reach "match over at frame 96948", hashdiff says **identical: 135000 frames**, resultDll (ovl 84) ran 13
times with the online checkpoints written (50 "checkpoint at frame" lines), and **no `save-*.png`** was shot, so
SLSaveQuiet / the icon never ran in a match. No window is gone from a match's hash log because a match never had it.

## Tried / failed

- Taking the brief's message: 0x24,0x07 is the results' `Ⓐ Next` prompt, not a saving message (above). Kept.
- First version suppressed only SLCreateSaveWin: the write's own "do not touch" window still popped up (test 3).
- PB_ICON_SHOT's `aurora_capture_frame` can't see RmlUi (EFB before RmlUi): that's why patch 0014 exists.

## Not done

- The other automatic saves keep their screen (filed as **N135**): mgmode's save (mgmode.c fn_1_6C18), story board
  clear (mstoryDll/mstory2Dll mg_clear.c), party end (mstory3Dll result.c). They are once per game, not per turn,
  and the brief asked for the per-turn one. The story "Would you like to save?" prompts are a choice and stay.
- No setting to get the old screen back (not asked).

## Not verified

- **A real two-peer lockstep session** (two processes over the network, or desktop + browser): not run. The check
  above is two `--match` runs of one build. A session can't reach the changed code either (save mode 1, test above).
- **Hash logs vs a main-branch build**: not compared. Offline `--script` runs vary from run to run (N53: rt and rt2
  took different minigame lengths), and offline the flow is meant to change at the save. Outside the save, the
  changed code only runs with `saveWinQuiet` set.
- **Card error paths under SLSaveQuiet** (no card, card full, wrong card): not forced. Their `SLMessOut` windows are
  not suppressed, and they now open over the results screen instead of the black save screen.
- **Story mode's per-turn save** (`SLSaveBoardStory` branch of SaveExecQuiet): no story run reached a save (a long
  scripted story game, as in N132). Same code path apart from the board blob.
- **Windows, Android, iOS, browser builds** of patch 0014 and the overlay: not built (load rule: one heavy job at a
  time). Plain C/C++ and the same wgpu/libpng calls as 0003's capture, which builds on all of them. On mobile the
  safe-area inset is the SDL one, as for Window/menu bar; not seen on a notched device.
- **PAL / JP discs**: only GMPE01 Rev 1 here.
