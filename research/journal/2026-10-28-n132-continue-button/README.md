# N132: Continue button on the Party Board main menu

User (2026-10-28): "in the main menu have a continue button for inprogress games that quick resume".

## What landed

**Launcher (src/port/ui/prelaunch.cpp, src/port/quick_continue_card.cpp).** When a disc is set, the Party Board menu
reads slot A's card before the game starts. It uses aurora's own card classes (`CardRawFile` / `CardGciFolder`), at
the path `CARDInit` would use (`<user dir>/MemoryCardA.<USA|EUR>.raw` or `<user dir>/<region>/Card A`), and only reads.
It also reads the N47 party autosave (`local/party.json`). Each game in progress gets a button **before Play**:
`Continue` (party) or `Continue Story`, with two detail lines: "Toad's Midway Madness, turn 2/20, file 1" and the
characters ("Yoshi, Luigi, Peach, Mario"; story: the player only). With nothing in progress there is no button and
the menu is as before. Several games in progress are a small pick list: one button each, at most 3. Focus starts on
the first one.

What counts as in progress is what mentDll's continue would offer:
- If the autosave exists, it is the one party entry. `netplay_party_offer` puts it in place of any card file's party
  when the menus continue, so card parties are not listed as well. Its file is the autosave's new `"box"` field if
  that file is on the card, else the first file on the card, else none (fresh GameStat, as with skipBootSequence and
  no file).
- Otherwise, each card file whose GameStat has `party_continue` gets a party entry.
- Each card file whose GameStat has `story_continue` gets a story entry.
- A file counts only if its checksum passes (`SLCheckSumCheck`'s sum).

**Game side (src/port/quick_continue.c, bootDll hook).** The press stores (kind, file) and closes the launcher.
bootDll, at its first boot (`omovlevtno == 0`), then does what the menus would do, step by step:
- File select (`FileSelectAutoLoadDefault` / `fn_1_37DC`): the card file into `saveBuf`, `SLLoadGameStat`, PAL
  language, save flag on.
- Mode select (`modeseldll main.c fn_1_414`): pads and `iscom`, the party/story pause options, flags (1,0/8/11/12),
  sound mode, `mgBoardHostEnableF`/`mgPracticeEnableF`/`mgQuitExtraF`. Also mentDll ObjectSetup's flag 0x1000B and
  `boardTutorialF`.
- Party continue (`fn_1_85C8` + `fn_1_C174(1)`): `netplay_party_offer`, then the check that `party_continue` is 1,
  `saveExecF = 1`, `SLLoadBoard`, `CharModelKill`/`MGSeqKillAll`, the `fn_1_7218` data preload (DATADIR_BOARD to ARAM,
  the board's dir), `CharMotionInit` x4, then `omOvlCallEx(board)`.
- Story continue (`fn_1_C4B8`'s "yes", main.c:2068-2133): `SLLoadBoardStory` and the same branches (ending, next
  turn, board clear/miss, map select, normal or Koopa event), with `fn_1_E654` / `fn_1_E71C`'s exits.
- Overlay history: as from the menus. Boot (1) -> modesel (1) -> mentDll (party 0 / story 1). For a board, the
  mentDll entry is renamed to mstory3dll, so the end of the board goes to the results and then the mode select.

If the file no longer has that game in progress, the boot goes on as usual. GWGameStat and the save flag are put
back, and the log says "continue: no party in progress in card file N, booting as usual".

**Boot hold.** With a verified disc the game starts *behind* the launcher (`Prelaunch::update` sets
`PartyBoard_IsGameLaunched`). By the time Continue is pressed, bootDll has already passed its first-boot check. So
while the launcher offers Continue (`quick_continue_offer`), BootExec's first boot waits until the launcher closes
(`QuickContinueHold`, `ui_is_prelaunch_open`). This happens only when there is a Continue button. With nothing in
progress, or with the launcher skipped, the game boots behind it as before. Play after a hold boots normally (title).

**Offline only.** No continue happens during a lockstep session or a match. `--script` runs count as offline: they
have `netplay_active()`, so that check is not used. Continue never opens the Online window.

**Test entry point:** `PB_CONTINUE=party|story[:box]` does the same as the button in runs without the launcher
(`--script` skips it). Without `:box`, the file is the one the launcher would pick.

**Autosave (`netplay_autosave.c`):** the JSON gets `"box": <curBoxNo>` (-1 without a card file). The format stays 1:
older readers ignore the field, and a missing field means "first card file". `netplay_party_autosave_peek` reads the
autosave without offering it. `tools/netplay/autosave_test.c` checks both and has fakes for `SLSaveFlagGet` /
`curBoxNo`.

Decisions made unattended:
- **Pick list, not "most recent".** Card files store `create_time` (OSTime), the autosave stores Unix time, and a file
  can hold both a party and a story. A list needs no mixed-clock ordering, and in practice it is one button.
- **The launcher reads the card itself** instead of keeping a cache file. The card is the truth, and the game checks
  it again at boot anyway (the fallback above).
- **Slot A only:** the PC game only uses slot 0 (skip-boot autoload, `HuCardInit` paths).
- **Hold the boot only when Continue is shown:** that is the one case where the background boot would be wasted.

## Tests

Disc GMPE01 Rev 1 (`$PB_DISC`), relwithdebinfo. Each run had its own user dir under `.n132-runs/` (deleted after;
the card image holds disc banner art), vsync off, `--turbo` for scripted runs. Runs were one at a time.
Harness: `harness/run.sh` (copies another run's user dir with `from <name>`; sets `WAYLAND_DISPLAY=wayland-1`, which
the agent shell lacks: without it SDL finds no video device), `harness/shift.py`, `harness/offdiff.py`.

1. **Save mid-party.** `mash-a.script --turbo` on a fresh dir: an offline w01 party, 20 turns. Card `MARIPA4BOX0`
   was written by resultDll after the first minigame (turn 1, `party_continue`). The autosave was at turn 2, player 1,
   `"box": 0`.
2. **Continue = resume through the menus (autosave).** Two copies of that dir:
   - `PB_CONTINUE=party` with the same script: the log says "continue: party board 0, turn 2/20, card file 0", and
     w01 starts at frame 4.
   - `mash-a` again with no env: boot -> modesel -> mentDll -> its "continue?" -> w01 at frame 2502.
   - First board frame of each: hash **524D1B2FFB64E510** in both. `sys`, `plr0-3`, `cfg`, `brand` and `pad` are
     equal; `frand`/`rand8` differ (not in the hash; the menus use them).
   - With the Continue run's pads shifted 2498 frames earlier (`shift.py`, so both runs get the same pads after the
     same board frame), `offdiff.py` gives **equal for all 14898 lined-up frames**. That covers the rest of turn 2,
     instDll, the minigame (ovl 22), resultDll and turn 3.
   - Unshifted, the runs split after 18 frames, in `pad` only (mash-a's taps are at fixed frame numbers).
3. **Continue = menus (card only).** The same with `local/party.json` deleted, so the card file's own party save
   resumes (turn 1/20, after the minigame). Hash **471EB5BF490E2F8A** in both at the first board frame. Shifted pads:
   equal for all 14898 lined-up frames.
4. **The launcher, end to end.** A temporary hook (`harness/ui_hooks.diff`, not committed) logged the menu's buttons
   and clicked one after 60 updates, in normal runs (launcher shown):
   - Save dir: the buttons are `Continue` ("Toad's Midway Madness, turn 2/20, file 1" / "Yoshi, Luigi, Peach,
     Mario"), Play, Online, Settings, Quit. Pressing Continue: bootDll was held, then "continue: party board 0, turn
     2/20", w01, "the board resumes at player 1's turn".
   - Pressing Play there: no continue, the title loads after the press.
   - Fresh dir: "0 game(s) in progress", the menu is Play/Online/Settings/Quit, and the game is not held.
   - With an extra fake story entry (hook `PB_N132_DUP`): two Continue buttons (`Continue`, `Continue Story`) before
     Play. Pressing the story one gave "no story in progress in card file 0, booting as usual", then the normal boot.
5. `tools/netplay/autosave_test.sh` (in check.sh): the `"box"` field (2 with a save flag, -1 without) and the peek,
   which leaves `party_continue` and the card buffer alone. `tools/check.sh` passes.

## Tried / failed

- First Continue run did nothing: `QuickContinueLoad` refused when `netplay_active()`, which every `--script` run
  has. It now refuses only when `netplay_lockstep()` or `netplay_match_running()` is true.
- The first launcher test showed bootDll already running behind the launcher before the press (see the boot hold).
- A GCI-folder card (`backend.cardFileType: 1`): mash-a's fixed-frame taps did not reach a saved party in that mode,
  with or without `USA/Card A` created first. No `.gci` file and no autosave were written, so the GCI read path was
  not run (see below).

## Not verified

- **A real story save.** No story run was played up to a save, which takes a long scripted story game. The story
  branches are a line-by-line port of `fn_1_C4B8` and were only read. Only the "no story in progress" fallback ran.
- **The GCI-folder card read** (`CardGciFolder` in the launcher): no GCI save could be produced (above). The raw image
  path (the default) was tested.
- **The look of the buttons.** Windows are on a hidden workspace, AGENTS.md now rules out gamescope/Xwayland
  capture, and `aurora_capture_frame` takes the EFB without RmlUi. The label RML was checked from the log. The rcss
  (two-line button, 16dp detail) and fit on small/mobile screens were not seen.
- **Touch / pad focus on the Continue button** by hand: focus goes to `mMenuButtons.front()`, which is now Continue,
  but nobody pressed it with a pad.
- **PAL disc** (EUR card path, language handling in the load): no PAL disc image here.
- **Windows, Android, iOS, browser builds:** not built. The new code is plain C/C++17 and aurora's own card classes.
  The browser skips the launcher, so there Continue only exists as `PB_CONTINUE`.
- **Offline play unchanged, by hash:** not compared against a main-branch build (offline `--script` runs vary between
  runs, N53, and the N47 reference hash predates later game changes). By construction, the game path changes only
  when the launcher shows Continue or `PB_CONTINUE` is set. The autosave file only gains a field.
- **A re-opened launcher mid-game** (N133's restart path): Continue acts through bootDll's first boot. Filed as
  **N134**.
