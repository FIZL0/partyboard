# N34: leaving an online match before its end

N25 already handled the party's own end (a menu overlay after the board: `pbnet_lockstep_finish`, `netplay_end`,
`pbnet_lobby_back`). What was left: the started lobby leaving `PBNET_LOBBY_STARTED` while the match runs. Nothing
called `netplay_end`, so the session (canned GWGameStat, netplay clock, `active`) stayed on and the board played on
with local pads.

## What was done

**Detection** (`src/port/netplay.c`, `netplay_lobby_poll`, every `ui_update`): if the lobby is no longer
`STARTED`, the poll had handled a started lobby (`lobby_handled`), and `netplay_match_running()` is true, then
`match_left` runs once. Every way out of a started lobby gets there:

- this peer leaves: `pbnet_lobby_leave` (state goes to NONE; the host's leave is "Save and Quit");
- a guest whose host saved and quit: the lobby holds (`host_left`, the "Host Left" modal) until the player presses
  "Leave Match", which is `pbnet_lobby_leave`;
- the lobby closes by itself (CLOSED).

A match that ended normally (N25) is no longer running when the lobby goes back to OPEN, so it does not count as
left. A drop or rejoin (N10) keeps the lobby STARTED, so it does not either.

`match_left`: `pbnet_lockstep_stop` (`pbnet_lobby_leave` has already stopped it; a CLOSED lobby may not have),
`netplay_end` (gives GWGameStat back, drops the still frame), then `netplay_match_leave`. A `--script` run keeps its
session (`netplay_match_end` only), the same way N25's `match_finish` does.

There is no common frame any more, so each peer goes back on its own. They all do the same thing: back to
modeseldll, the overlay the party's own end reaches.

**Going back to modeseldll** (`src/port/netplay_match.c`, `netplay_match_leave` / `netplay_match_leave_poll`,
polled from `netplay_lobby_poll`). This is a small state machine, reset whenever the overlay changes:

- During an overlay switch (`omnextovl` set or `omcurovl == DLL_NONE`) it waits.
- **Board overlay** (w01..w06): it waits until the board runs (flag (1,16)) and is not already ending itself (1,17:
  going to a minigame). Then it sets flag (1,27) and `omSysExitReq`. That is the game's own reset path:
  `ExecBoardFunc` calls `BoardKill` (wipe out, `DestroyBoard`), and `KillBoardFunc` does `HuARDirFree(DATADIR_BOARD)`,
  clears (1,0) and, because of (1,27), `omOvlReturnEx(2)`. The overlay history is MatchStartProc's
  [boot, modesel, mstory3, board], so it lands on modeseldll and skips mstory3dll's results. The board pause menu's
  own "quit" does the same (pause.c: `_SetFlag(0x1001B)` then `BoardKill`).
- **Any other overlay** (instDll, a minigame, resultDll, a board that has not run within 600 polls): it creates
  `MatchLeaveProc` at priority 0xFFFF, the mirror of `MatchStartProc`. That lifts the system pause (`HuPrcAllPause`,
  `HuPrcAllUPause`, 3D, sprites, FX, seq), kills the overlay's own processes (`HuPrcKillIf(overlay_process)`), resets
  the wipe, stops the sound, frees the board's ARAM data, clears (1,0), and sets the history to [boot, modesel] and
  `omOvlGotoEx(DLL_modeseldll, 1, 1, 0)`.
- When **modeseldll runs**, it puts back the GWGameStat that `netplay_end` returned (the board's teardown can write
  the match's settings into it: `PauseExit` writes `party_pause`). It also closes the minigame data that the board
  (`BoardMGSetup`) or instDll read with `HuDataDirReadAsync` (`DATADIR_INST` and `mgInfoTbl[GWSystem.mg_next]`, as
  resultDll does), but only if no read is still in flight (`HuDataReadChk`).

**Debug**: `--leave-at <frame>` leaves a `--match` at that frame as if its lobby had gone.

**Tests**:

- `tools/netplay/tick_gate_test.c` `match_left_checks`: lobby going NONE, CLOSED or OPEN while the match runs stops
  the lockstep, ends the session and leaves once. It does not leave when the match is not running.
- `tools/netplay/match_test.c` N34 block: the board waits for (1,16) and not (1,17), then sets exit req + (1,27) once.
  It waits during an overlay switch. At modeseldll GWGameStat is the player's and the INST + minigame dirs are
  closed. From a minigame: one leave process, pauses lifted, ARAM freed, history [boot, modesel], modeseldll with
  event 1. The board fallback switches after 600 polls.

## In-game checks (GMPE01, `tools/netplay/save-continue.py save` script, `--match 0@900`)

| leave at | overlay | path | result |
|---|---|---|---|
| 3000 | board (89) | omSysExitReq -> BoardKill | modeseldll (74), then the A taps go on into overlay 70 |
| 6400 | instDll (3) | MatchLeaveProc (killed 5 processes) | same |
| 6800 | minigame (14) | MatchLeaveProc | same |
| 1650 / 2150 / 2700 / 3150 / 3650 | board | omSysExitReq | same |
| 6800 / 6900 / 7000 / 7100 | minigame | MatchLeaveProc | same |

Heaps at the overlay after modeseldll: the DVD heap is back to 1 block on every path (see below). The DATA heap keeps
about 10-60 more small blocks (12 KB of 44 MB) after a leave from instDll or a minigame than after one from the board.
A normal party grows the DATA heap's block count the same way (41 -> 147 over board -> inst -> minigame -> results ->
board), so these look like the game's own caches. They were not traced further.

## What failed on the way

- `omAllPause(FALSE)` in `MatchLeaveProc`: SIGSEGV. It works on the calling process's objman (`HuPrcCurrentGet()->
  user_data`), and the leave process has none. It was removed: the overlay's objects go with its objman in
  `omOvlKill` anyway.
- The first hard switch from instDll or a minigame left a 2.2 MB block in the DVD heap. gdb on `HuMemDirectMalloc
  (HEAP_DVD)` showed it is the `HuDataDirReadAsync` buffer of the next minigame's data (instDll:277; the board's
  `ExecMGSetup` reads a 1.5 MB one first). resultDll closes it after the minigame, and a hard switch skips that. Fixed
  by the close at modeseldll above.
- Pause tests: one START tap on the board at 1500/2000/2500/3000/3500 and in the minigame at 6600..7000. gdb
  breakpoints on `HuPrcAllPause(1)` and `BoardPauseStart` never fired, so no run actually left from a paused state
  (see Not verified).

## Not verified

- **A real two-peer leave.** Not run: host "Save and Quit", guest "Leave Match" after "Host Left", and a CLOSED lobby
  mid-match all went untested with real pbnet peers. The lobby-to-leave path is covered by the fake-pbnet harness
  (`tick_gate_test`), and the in-game half by `--leave-at`, which calls the same `netplay_match_leave` after
  `netplay_match_end` instead of `netplay_end` (a scripted run keeps its session). A two-process run needs the
  scratch libpbnet host setup (N33 journal) plus clicking the modal, and was out of this session's budget.
- **Leaving while paused** (minigame START pause, board pause menu). No scripted START tap opened a pause (see
  above), so the pause lifting in `MatchLeaveProc` and `BoardKill` with an open board pause menu are untested in
  game. They are covered only by the stub test, which checks that the unpause calls are made.
- **What it looks like.** No screenshot was taken: AGENTS.md rules out the gamescope route. Evidence that modeseldll
  is live: the scripted A taps after the leave take it on into overlay 70 on every path.
- **A second online match after a leave.** `--match` starts only one match per run. The state a new match needs
  (`match` struct cleared, `lobby_handled` reset, GWGameStat own) is checked in the harnesses only.
- **Offline play.** Not re-run against a baseline hash log. No offline code path changed: the one new per-frame call
  (`netplay_match_leave_poll`) returns at once unless a leave is under way, and `match_left` needs a started lobby.
- **The UI after a leave.** It is whatever the lobby UI already does (`leave_to_menu` opens the Online window; the
  "Host Left" modal just closes). No toast says the match was left, and that was not added.
