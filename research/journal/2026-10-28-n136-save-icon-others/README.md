# N136: saving icon for the other automatic saves

N129 follow-up. N129 replaced resultDll's per-turn save screen (black, WIN 0x20 "Saving" sprite, SLSave's
`Slot A: Saving!` window) with `SLSaveQuiet` + the port's saving icon. The brief listed four more screens.

## Per screen

| Screen | What it is | Decision |
|---|---|---|
| mgmodedll `fn_1_6C18` | Leaving Mini-Game mode after something in GWGameStat changed (records, unlocks): white wipe-out, black + WIN 0x20 sprite, `SLSave` with its windows, wipe-out | **Quiet** (TARGET_PC): `fn_1_6C18Quiet` saves over the menu before the 60-frame white wipe-out; `fn_1_6C18` is no longer called on PC |
| mstoryDll `mg_clear.c` `fn_1_F86C`, mstory2Dll `mg_clear.c` `fn_1_13828` | Black screen, 60 frames, `SLSaveBoardStory` + `SLSave` | **Unchanged: dead code.** Nothing in the tree calls them or sets/reads their done flags (`lbl_1_bss_55C`, `lbl_1_bss_794`) apart from the functions themselves (grep of src/ and include/). The story board clear goes `fn_1_F910`/`fn_1_138CC` → DLL_present with no save. The decomp matches the GC binary, so the game never ran them either |
| mstory3Dll `result.c` `fn_1_1712C` | Party end (`GWPartyGet() == 1`): after the results' black wipe-out, a child process with the WIN 0x20 sprite screen, `fn_1_159A4` (board records), `SLSave`, wipe-out | **Quiet** (TARGET_PC): `fn_1_1712CQuiet` (the same statements with `SLSaveQuiet`) runs in `fn_1_17570` **before** the wipe-out, right after `netplay_party_finish()` (moved up with it, same order as before: finish, then save). The child process isn't created on PC; the `fn_1_958(30)` after it stays |
| mstory3Dll `fn_1_1728C`, mstory2Dll `ending.c` `fn_1_199A0`, mstoryDll/mstory2Dll `save.c` | Story "Would you like to save?" (message 30,22, `HuWinChoiceGet`) | **Kept** (the brief: user choices). Their sprite screen is the backdrop of the question |

Why save *before* the wipe-out (as N129 does): after a wipe-out, wipe.c's `WIPE_MODE_BLANK` fills the screen every
frame, so a card error window (`SLMessOut`: no card, wrong card, full) opened under it could not be seen or answered.
Saving first puts those windows over the menu / results, which are still up.

`fn_1_159A4` (total stars, board play/win counts, max stars/coins) now runs before the wipe instead of after it.
It only writes GW/GWGameStat, which nothing in the results' wipe reads (the result objects are cleared after the
wipe). SLSave's stack: the save now runs in the results' object process (fn_1_18080) instead of a 0x2000 child; on
PC every process stack is at least 0x10000 (process.c), the same as the child had.

## Tests

Disc GMPE01 Rev 1 (`$PB_DISC`), relwithdebinfo. Harness in `harness/`: `run.sh` (N129's, run dirs under
`.n136-runs/`, `CONFIG=` for config.json, `GDB=` to run under gdb -batch), `mgmode-exit.script`, `mgmode-force.gdb`,
`shots.gdb` (a game + RmlUi shot every 100 script frames, `sed s#SHOTS_DIR#<dir>#` first).

1. **Party end + online check**: `WAYLAND_DISPLAY=wayland-1 PB_ICON_SHOT=... PB_SAVE_SHOT_DELAYS=2,15,30,60
   tools/netplay/party_check.sh` (two `--match` w01 parties, 10 turns, turbo). Both: "match over at frame 96948",
   **identical: 135000 frames** (the frames after the party-end save included). Each run shot one save
   (`save-00`), right after "match over" (the results, ovl 78): d002 = RESULTS screen, d015 = results darkening with
   the icon top right, d030/d060 = black wipe with the icon. No WIN 0x20 picture, no `Slot A: Saving!` window. The
   card's last write (19:08:15, run started 19:03:34, ~386 frames/s) falls when the run left the results
   (~frame 101400), so the save wrote the card.
2. **Minigame mode exit**: `mgmode-exit.script` (wander-mgmode's boot into Mini-Game mode, skipBootSequence on) under
   `mgmode-force.gdb`: at script frame 2500 it sets the menu's quit flag `lbl_1_bss_2D0` (what B + Yes sets), and at
   fn_1_54F4's memcmp flips a byte of the GWGameStat backup and sets `SaveEnableF = 1` (this run had no card file:
   "File select skipped ... result -4", so saves were off). Backtrace: `fn_1_54F4 → fn_1_6C18Quiet → SLSaveQuiet →
   SLSave`; SLSave took the create-file path and the card now holds `MARIPA4BOX0`. Shots: d002 the mgmode menu, d015/
   d030 the menu fading to white with the icon, d060 white with the icon. No sprite screen, no window from the save
   (the create path's "do not touch" window included). The menu's own "Play" text window is still in the shots only
   because the quit was forced: B + Yes closes it before setting the flag.

## Tried / failed

- An offline whole party (`party.script` without `--match`, end 400000, turbo) stalled in overlay 3 after turn 1
  (party.script's taps are tuned for a `--match` party), and an offline party is 20 turns. Used party_check.sh's
  `--match` parties instead; their party end takes the same `GWPartyGet() == 1` branch.
- Leaving Mini-Game mode by script: B at 900, then at 1500 (the host's intro talk runs until ~1650), then at 2000
  with the stick up to Yes and A: the menu never showed the quit question (shots every 100 frames via
  `shots.gdb`). Not chased; the gdb quit flag runs the same fn_1_54F4 exit.
- party_check.sh failed at first with "No available video device": the agent shell has no WAYLAND_DISPLAY
  (party_check.sh doesn't set it, run.sh does).

## Not verified

- **Minigame mode's real save trigger** (a record or unlock changing GWGameStat) and the real B + Yes exit: forced
  from gdb instead (above). The forced path runs the same code from the memcmp on.
- **Card error windows** (no card, full, wrong card) under the new quiet saves: not forced. `SLMessOut` is not
  suppressed and now opens over the mgmode menu / party results.
- **Party end offline** (not in a `--match`): the code has no netplay branch apart from `netplay_party_finish()`,
  which returns early in a match; the offline call (autosave deleted, `party_continue` cleared before the save, as
  before) was not run to a party end.
- **Story mode party end**: `fn_1_17570`'s story branch is unchanged (its save is the `fn_1_1728C` question).
- **Windows, Android, iOS, browser builds**: not built (load rule, one heavy job at a time). Plain C in the game
  overlays, same calls as N129's resultDll change.
- **A real two-peer session**: not run. party_check.sh is two `--match` runs of one build; the party-end save runs
  after "match over", outside the lockstep session.
