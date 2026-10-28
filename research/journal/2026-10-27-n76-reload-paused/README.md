# N76: a reload while a peer is paused

## Can a lockstep pause happen?

Yes. Nothing in netplay gates the two START pauses:

- **Minigame:** `omSystemKeyCheck` (objsysobj.c) takes START on any pad when `omSysPauseEnableFlag` is set. Then it
  pauses every process (`HuPrcAllPause`), the 3D (`Hu3DPauseF`), sprites, FX and music, and starts the dol process
  `PauseProc` (minigame_seq.c, `MGSeqPauseInit`).
- **Board:** `BoardPauseReqCheck` -> `BoardPauseStart` opens the pause menu (a child of `boardMainProc`).
  `omSysPauseCtrl(1)` then makes the next `omSystemKeyCheck` take the same system pause.

The pads are shared in lockstep, so every peer pauses in the same frame. One peer is paused and another is not when
a reload comes during the pause: the rejoining or fresh peer was never paused. All peers reload, and the ones that
were paused keep the pause. I found this by reading the code; I did not run it with two peers (see Not verified).

## What a paused peer kept after the reload (before the fix)

`MatchStartProc` killed the overlay's processes, but nothing ended the pause:

- `Hu3DPauseF` stayed 1 on the new board. Only `Hu3DInit` clears it, and the board's new `omSystemKeyCheck` object
  starts unpaused, so it never lifts the pause. The board's models then did not animate, and the game state
  diverged.
- `HuSprPauseF` was cleared by `omOvlKill`'s `HuSprClose`. The process pause flags went with the killed processes.
- The minigame's `PauseProc` lived on into the board. It is a dol process, so `HuPrcKillIf(overlay_process)` does not
  kill it, and it is not a child of omwatchproc, so `omOvlKill` does not either. It kept waiting for `pauseExitF`. If
  it ever ended, it would `HuSprGrpKill` its old group id and `HuWinKill` its old windows, which by then belong to
  the board.
- `MGSeqKillAll` (called in `MatchStartProc` and `omOvlKill`) calls `MGSeqMain`, which returns at once while
  `Hu3DPauseF` is set. So the minigame's sequences were never ended either.

Measured with `harness/run.sh` (one process: mash-a, `--match 0@900 --turbo --hash-log`, under gdb). The run
presses START for `omSystemKeyCheck` at frame 11400 in minigame ovl 23, or makes `BoardPauseReqCheck` return 1 at
board frame 3000, and then reloads (`netplay_match_start`, the call a resync makes) 100 frames later. The
comparison is the same reload without the pause, which stands in for the fresh peer.

| case | after the reload (before the fix) | hash log after the reload vs no pause |
|---|---|---|
| minigame pause, reload at 11500 | Hu3DPauseF 1, PauseProc alive (to 11897+) | 5899/5899 frames differ (from 11501) |
| board pause menu, reload at 3100 | Hu3DPauseF 1 | first difference at 3857 (plr state), 13160/14299 differ |

## Fix

- `match_pause_lift()` (netplay_match.c) lifts every pause that `omSystemKeyCheck` sets. It is the first thing
  `MatchStartProc` does, before the kills and `MGSeqKillAll`. `MatchLeaveProc` (N34) had the same six calls inline;
  it now calls the helper instead.
- The helper also calls `MGSeqNetplayPauseKill()` (new, `TARGET_PC`, minigame_seq.c). It kills the pause screen
  process without running it and resets that screen's statics. Its windows and sprites are freed with the overlay
  (`omOvlKill`).
- The board's pause menu needs nothing more: its `PauseExit` destructor already runs at the reload (N62's
  `HuPrcChildDtorRun`), and N66's `BoardPauseNetplayReset` resets its statics.
- `tools/netplay/match_test.c`: the stub test checks that a match start lifts all six pauses and ends the pause
  screen before the board is called, and that a leave ends it too.

## After the fix

| case | after the reload | hash log vs the unpaused reload |
|---|---|---|
| minigame pause, reload at 11500 | all pauses 0, no PauseProc | 5899 frames identical (heap included) |
| board pause menu, reload at 3100 | all pauses 0 | 14299 frames identical (through the next minigame and back to the board) |
| unpaused reloads, before vs after the fix | — | 17400 frames identical (both cases) |
| minigame pause, then `netplay_match_leave` at 11500 | "ended the minigame's pause screen", modeseldll, all pauses 0 | — |

Each case was run once (GMPE01 Rev 1).

## Not verified

- **A real two-peer resync while paused.** I did not run a resync through the netns harness (N38/N52/N56) with one
  peer paused and a rejoining guest. Starting a resync from the network at a chosen paused frame needs the full
  two-peer kit. The one-process runs go through the same `netplay_match_start` -> `MatchStartProc` path that the
  resync uses. They compare against an unpaused reload, which is the fresh peer's state.
- **A real START press from a pad, and the board menu opened from START.** The pauses were forced from gdb: START
  was injected into `HuPadBtnDown[0]` for `omSystemKeyCheck`, and `BoardPauseReqCheck` was made to return 1. The
  code after that is the game's own. As N34 found, scripted START taps never opened a pause.
- **A reload while the pause screen is closing** (`pauseExitF` set, `PauseProc` animating out), and a reload in the
  practice-quit path (Z). Not run. The kill is the same in both.
- **Sound.** FX and music are unpaused, and paused minigame sound effects are not stopped (`MatchStartProc` only
  fades out music and streams, as it did before). Nothing checked by ear.
- **Other platforms** (Android, Windows, macOS, iOS): not run. The change is plain C.
