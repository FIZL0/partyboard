# N62: a resync reload while the board runs no longer crashes

**Outcome: fixed.** I reproduced N42's crash in a single process with the exact same stack, and it is gone after the fix.
The reload resumes the turn and the board goes on.

## Cause

`MatchStartProc` (`src/port/netplay_match.c`) runs at frame 0 of a reload and calls `omOvlCallEx(board)`. That call
runs `omOvlKill` at once: `Hu3DAllKill`, `HuWinAllKill`, `HuSprClose`, and only then `HuPrcChildKill(omwatchproc)`.
A kill only marks a process. Its destructor runs later, when `HuPrcCall` reaches it (`HuPrcEnd` ->
`gcTerminateProcess`). On a reload from a menu this does no harm. On a reload while the board runs, the board's
processes (libdol code, under `boardObjMan`, so `HuPrcKillIf(overlay_process)` keeps them) have destructors that touch
board models. `DestroyShop` (shop.c:390) calls `BoardModelHookReset(shopMdlIdx)`. The board model table still names
Hu3D model 54, which `Hu3DAllKill` has freed, so the call crashes with SIGSEGV in `Hu3DModelHookReset`.

When the board exits by itself, this never happens: `BoardKill` kills `boardMainProc`, and `KillBoardFunc` waits for
`boardMainProc == NULL` (all destructors done) before it calls `omOvlCallEx`.

## Fix

- `HuPrcChildDtorRun(root)` (new, `src/game/process.c`, `TARGET_PC` only): runs the destructors of every descendant of
  `root` now, in process-list order (the order `HuPrcCall` would end them). Each one runs with `processcur` set to its
  own process, as in `HuPrcEnd`, and is then cleared, so the later kill runs none. After each destructor it starts the
  scan again, because a destructor may kill or create processes.
- `MatchStartProc`: if `omcurovl` is a board (w01..w06) and `boardObjMan` is set, it calls
  `HuPrcChildDtorRun(boardObjMan)` before `omOvlCallEx`, and logs
  `netplay: match start: ran N board process destructors`. This covers `boardMainProc` and its children (shop,
  lottery, ...) and the board REL's processes under the objman. It does not run the objman's own destructor
  (`omDestroyObjMan`), which stays with `omOvlKill`.
- I did not call the game's own `BoardKill`/`DestroyBoard`. `BoardKill` sleeps on a wipe, which would put the reload
  one frame later on a peer that is on the board than on one in a menu (a desync). The destructors alone are enough:
  `omOvlKill` frees everything else, as it did before. N38's `netplay_ovl_start` sets the state again from the match
  in either case.
- Offline play is unchanged. The new code only runs from `MatchStartProc` (netplay), and `src/game/process.c` only
  gains a function.

## Checks

The gdb scripts are in `harness/`. Every run below was `--script tools/netplay/mash-a.script --match 0@900 --turbo`
under gdb, with `$PB_DISC`, its own HOME/XDG, and ASLR on. In each run, player 1 entered the shop on w01 at frame
14998 (`break ExecShop`). 120 frames later, at frame 15118 inside the shop, gdb forced a reload.

| run | reload | result |
|---|---|---|
| `cont-old.gdb` | `netplay_match_continue` with the turn blob (`netplay_checkpoint_last`: turn 2, player 1), which is the resync path. The new destructor pass is skipped from gdb (`return 0`). | **SIGSEGV** `Hu3DModelHookReset(arg0=54) <- BoardModelHookReset(model=46) <- DestroyShop (shop.c:390) <- gcTerminateProcess <- HuPrcEnd <- HuPrcCall`. The same stack as N42's. |
| `cont-new.gdb` | same, with the fix | `ran 3 board process destructors`, `board overlay 89 starts at frame 15119, turn 2/10`, `the board resumes at player 1's turn`. Player 1's turn finishes and player 2's starts at frame 17130 (checkpoint). The script ends at 17400 and the process exits normally. |
| `reload-old` / `reload-new.gdb` | `netplay_match_start` (from the match start) in the shop | old: the same SIGSEGV. New: the board starts again, and the run ends normally. |
| `multi.gdb` | 5 reloads in one run (frames 2600, 4100, 6000, 8000, 10500) | 3–5 destructors each time, no crash, normal exit. All 5 landed in the board intro (`player_curr -1`), because each reload restarted the board. |

`tools/netplay/match_test.c` has new stubs, and a test that a reload with a board as the current overlay runs the
destructors for `boardObjMan` **before** `omOvlCallEx`, and runs none from a menu or with no objman.
`tools/check.sh` passes.

## Tried / failed

- The first `tools/check.sh` failed to link `match_test`: it stubs the game, and needed `boardObjMan` and
  `HuPrcChildDtorRun`. I added both.

## Not verified

- **Two peers.** N42 saw the crash with two real peers after a real desync. I reproduced and checked it in one process,
  forcing the reload from gdb with the same calls the resync makes (`netplay_match_continue` with the turn blob, then
  `MatchStartProc`). I did not set up a two-peer run, because N42's turn-1 desync is not reproducible at will (N53) and
  the code path on each peer is the same.
- **Reloads in other board states** that have destructors: the lottery, the Boo house, item use, battle and Bowser
  events, and other boards (w02..w06). Only the shop on w01 and the board intro were hit. The other destructors now run
  earlier, while their models still exist. That is the same order as the board's own exit, but I did not run each one.
- **Hash equality across peers after such a reload.** The destructors change models and windows before the overlay
  change, on the board peer only. The state that is hashed is set again at the board start (N38), but I did not
  compare a board peer's hash log with a menu peer's around a reload.
- Windows/Apple/Android: not built (no SDKs). The change is plain C under `TARGET_PC`.
