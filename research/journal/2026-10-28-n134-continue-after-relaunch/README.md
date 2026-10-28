# N134: Continue from a re-opened Party Board menu (after N133's Quit to Main Menu)

## Question

N132's Continue buttons are built in `Prelaunch`'s constructor and act through bootDll's first boot
(`QuickContinueHold` / `QuickContinueLoad` at `omovlevtno == 0`). N133's Quit to Main Menu pushes a new
`Prelaunch(true)` and warm-reboots the game into bootDll. Does that menu rescan the card, and does its Continue reach
the board through the restarted boot, with no stale request?

## Reading (src/port/ui/menu_bar.cpp `quit_to_launcher`, src/port/netplay_match.c `MatchRebootProc`)

- The menu is a new `Prelaunch` object, so its constructor runs `quick_continue::scan` again (card + N47 autosave)
  at quit time, and `quick_continue_offer` is set from that new list.
- The reboot is `omOvlGotoEx(DLL_bootdll, 1, 0, 0)`: **event 0**, so `BootExec` runs `QuickContinueHold` (holds while
  the menu offers Continue and is open) and then `QuickContinueLoad`, as on the first boot. `BootInitForSkippedSequence`
  skips its system init when `SystemInitF` is already set. `launch_game` with `mRelaunch` skips `HuCardInit`.
- The launcher's request is cleared by `QuickContinueLoad` each time it is read, and `pending.loaded` by
  `QuickContinueStart`, so one launcher pick cannot carry over to a later boot.
- **One stale path found: `PB_CONTINUE`.** `request_from_env` ran on every event-0 boot, so in a test run with
  `PB_CONTINUE` set, every N133 reboot would have continued again by itself without a press. Fixed: it reads the env
  once per process (src/port/quick_continue.c).
- The scan runs at quit time, while the board is still leaving (a few hundred frames). Nothing on that exit path
  writes the card or the autosave (the autosave is written at turn starts, deleted only by the party's results), so the
  list is not out of date by the time bootDll runs. Kept as it is.

## Changes

- `Prelaunch::press_continue()` (the first Continue button) and `continue_game(entry)`, which the button's lambda now
  calls (same code as before, moved). `prelaunch.hpp` includes `port/quick_continue.h` for the stored entry list.
- N133's `PB_RELAUNCH_TEST` hook takes a `continue` step (src/port/ui/menu_bar.cpp). An unknown step name (`x` below)
  does nothing but still waits its turn, which is handy for "after the results" anchors.
- `PB_CONTINUE` read once (above).

## Checked (desktop, GMPE01 Rev 1, relwithdebinfo, `--turbo`, fresh user dir each run in the session scratchpad)

Harness: `harness/run.sh` (user dir, vsync off), `harness/longer.py` (mash-a.script past its end at 17400, A taps
until END), N133's `relaunch_script.py` (A+START taps after a frame, for the rebooted title).

1. `mash-a.script`, `quit@89+3000,continue@+300`: autosave turn 1/20 player 0 → quit on w01 → board leaves → reboot →
   bootDll held → Continue: "continue: party board 0, turn 1/20, card file 0" → "the board resumes at player 0's
   turn", the turn goes on (autosaves players 0-3). The launcher at start had nothing in progress (fresh dir), so the
   entry came from the re-opened menu's own scan.
2. `longer.py 26000`, `x@84+100,quit@89+1000,continue@+300,quit@89+900,continue@+300`: after the first minigame's
   results, autosave turn 2/20 player 0, quit, Continue → turn 2/20, player 0; quit again from the continued board,
   Continue again → turn 2/20, player 0, then players 1-3 play their turns. Two cycles, no stale state.
3. `longer.py` + `relaunch_script.py 19500 30000`, `x@84+100,quit@89+2200,play@+300,quit@74+200,continue@+300`:
   quit **during player 1's** turn 2 (last autosave: turn 2/20, player 1). **Play** on the re-opened menu (Continue
   was offered): the hold let go, the title booted, no `continue:` line. Title → modeseldll → quit → Continue →
   "continue: party board 0, turn 2/20" → "the board resumes at player 1's turn": the same turn and player as at the
   quit.
4. `tools/check.sh` passes.

"GroupSet Error 0" lines show up at every overlay load in these runs, before any quit too; not from this change.

## Not verified

- **The same state hash** as the board at the quit: the resume comes from the autosave written at that player's turn
  start (N47/N132 compared that path by hash, 14898 frames); here only turn/player from the logs were compared,
  progress since the turn start is lost by design (as it is on any continue).
- **Story continue after a reboot:** no story save was produced (N132 had none either).
- **Online quit then Continue:** QuickContinueLoad refuses while `netplay_lockstep()`; the N133 online quit was never
  run (needs a two-peer lobby), so neither was this.
- **Android / iOS / Windows:** not run; the change is plain C/C++ in shared code. The browser has no prelaunch menu.
- **A real pad/touch press** of the re-opened menu's Continue: the test hook calls the same `continue_game` as the
  button's callback, but no physical press was made (windows are on a hidden workspace).

## Follow-up filed

- N135: c076e857 (N133) committed `.n133/` test user dirs (16 MB card images with banner art, caches, logs).
