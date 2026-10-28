# N78: reload mid-event, the cases N66 could not run

**Outcome:**
- 92 one-process reloads in `cont` mode (the resync's `netplay_match_continue`), plus 2 in `start` mode. They cover:
  boards w02..w06, every item 0..10, the last-5-turns event, the board pause screen, and the board's return after a
  battle or Bowser minigame.
- None crashed. Every board start after a reload logged a fresh board's heaps (`BDDB26B214B20C84`), and every run
  resumed a turn point. Apart from the board-pause runs, all played on to the end of the script.
- At the first turn point after the reload, `diff.py`-style dumps show **no stale handle**, with two exceptions:
  - **Bowser suit motions:** item.c's `suitMot`/`suitCommonMot` were left by the reload. They are now in
    `BoardItemNetplayReset`.
  - **Board pause: a reload in the board pause screen freezes the board** (`Hu3DPauseF` stays 1). That is N76's
    case, and it is left to N76 (details below).

GMPE01 Rev 1, `build/linux-default-relwithdebinfo`, N66's harness with mash-a and `--match <board>@900 --turbo`.

## What I ran

N66's `reload.sh` takes two new knobs: `BOARD` (the `--match` board, default 0) and the default `DUMPLINE` 748. That
is the `leftovers_free()` line right after `board_statics_reset()`; N67 and N66 moved it from 727. `harness/` here
adds the following:

- `case.sh`: one run. Its arguments are name, board, mode, EVENT and DELAY, then FORCE files (`;`-separated) from
  either directory. `cases.txt` lists all the runs (`xargs -P6 -L1 harness/case.sh <outroot> < harness/cases.txt`).
  EVENT may carry a gdb condition, with `~` for spaces.
- New FORCE files:
  - `force-last5.sh N`: `max_turn` N at the intro. 6 runs the event in turn 2, after the minigame; 5 runs it in turn
    1, which has no checkpoint before it, so turn 1 needs `start` mode.
  - `force-pause.sh F`: `BoardPauseReqCheck` answers yes for pad 0 from frame F on.
  - `force-bowser.sh [mg]`: the Bowser space becomes Bowser's minigame event, optionally with that minigame.
  - `force-star.sh p`: the first space player p passes becomes the star's, and p gets 100 coins.
  - `force-allcom.sh`: four COMs.
  - `force-dumpall.sh`: dumps at every turn point.
- Probes: `probe-pause.sh`, `probe-procs.sh`, `unpause3d.sh`.
- `summary.py`: one line per run (reload, signal, heaps, resumed turn point, end hash). Its output is
  `summary.txt`.
- `report.py`: the statics the reload left (`stale.py`) and the handle differences at the resumed turn point. Its
  output is `report.txt`.

**What `report.py` compares, and why it is not N66's `diff.py` against the fresh turn 1.**
- A `cont` reload skips the board intro, so the fresh board's first turn point is the wrong reference. It still holds
  the intro's leftovers (`start.c` `hostMdl`/`logoSpr`, `ui.c` `yourTurnSpr`, model counts).
- So each board has a fresh run that dumps every turn point (`fresh<board>`, `force-dumpall.sh`). A static counts only
  if it differs from **every** fresh turn point, with a pointer on one side only or a handle-like name.
- The last-5-turns runs use fresh runs with the same forced `max_turn` 6 (the event's models and objects stay up after
  it, on a fresh board too).
- These always differ and are written before every read, so they are listed as benign: `start.c` `hostMdl` and
  `logoSpr`, `ui.c` `yourTurnSpr`, `window.c` `winPosIdx`.

## Results (summary.txt, report.txt)

| cases | runs | reload during | result |
|---|---|---|---|
| w02..w06: blue, walk, mg_setup, happening (forced, +90 and +300 frames), Bowser space, battle space, star (forced) | 40 | the board's REL events (`landEventFunc`) and the dol helpers they call | all resume and play on to a minigame; no stale handle |
| items 0..10 on w01 (+30 and +150 frames), and each on one of w02..w06 | 33 | `BoardItemStart` (mini, mega, super mini/mega, hammer, pipe, swap, spark, light, whistle, Bowser suit) | all resume; each item's own statics show up in the left list (pipe/swap `charWheelObj`, suit `bowserSuitObj`, light `diceObj`) and are reset. Bowser suit: `suitMot`/`suitCommonMot` were not (fixed, below) |
| last-5-turns, w01 and w06, turn 2 (+60, +400) | 4 | `BoardLast5Exec` (the roulette's models, objects) | resumes turn 1 player 3; the new board runs the event again; no handle difference against a fresh last-5 board |
| last-5-turns, turn 1, `start` mode, w01/w06 | 2 | the same | plays on |
| battle return, w01/w03 (+30, +200 frames) | 4 | `BoardBattleExec` with flag (1,4), after the battle minigame | resumes; no stale handle |
| Bowser minigame return (m437), w01/w03 (+30, +200 frames) | 4 | `BoardBowserExec` with flag (1,3) (`ExecMGReturn`) | resumes; no stale handle |
| board pause, w01 (+20, +90), w04 (+40) | 3 | `BoardPauseStart` | **the board stops**: see below |
| the same with `Hu3DPauseF` cleared from gdb | 2 | | plays on; w01 ends on `D91EE5439FF5E594`, the same hash as N66's turn-1 resumes |

**Differences `report.py` still lists, each written before it is read** (checked in the source):
- `battle.c`: `battleCoinIdx` and `battleMGIdx`, set at each `ExecBattle` (lines 197, 835), the return path included.
- `bowser.c` `playerMot`, `star.c` `hostMot`, `last5.c` `hostMot`: -1 after the event's own kill (a clean value).
  Fresh boards keep 0, the bss value. Each event creates them before use (bowser.c:122/1293, star.c:181, last5.c:449).
- `shop.c` `shopMdlPtr` (w05): it points into `shopMdlIdx` (dol data), and every shop event sets it first
  (shop.c:217/1167).
- `window.c`: `winAttr`, `winPause`, `winPrio`, `comKeyFunc` and `winInsertMesTbl` are the last window's values.
  Every `BoardWinCreate` sets them again (window.c:218–275). After a battle return, `winInsertMesTbl[0]` holds a heap
  pointer, read only by that window's process.
- `com_path.c` `numCandidates`: a search table, left as it is by design (N66).

## Fix: the Bowser suit's motions

`item.c` `suitMot[2]` and `suitCommonMot[2]` are motion handles. `ExecItemBowser` creates them, and
`RestoreItemBowser` (the suit's turn-end restore) uses them without creating them. A reload mid-suit left `{1, 2}`
from the old board; a fresh board has `{0, 0}`.

A resumed turn point is always before the suit is put on, so I found no path that reads the old values. Still, they
name objects, so by N66's rule they go into `BoardItemNetplayReset`. After the fix, the three item-10 runs show
`suitCommonMot={1, 2}` in the left list and no difference at the turn point. Their end hashes are unchanged
(`D91EE5439FF5E594`, `4C9BE05A121034D1`).

## Board pause: the reload freezes the board (N76)

`BoardPauseStart` calls `omSysPauseCtrl(1)`. The board's system key object (`omSystemKeyCheck` on `boardObjMan`)
then pauses everything:
- `HuPrcAllPause(1)`
- `Hu3DPauseSet(1)`
- `HuSprPauseSet(1)`
- the audio pauses

Only that same object lifts them. When the pause screen ends, `PauseExit` sets 0x100 and the object unpauses on its
next frame.

A reload kills `boardObjMan` with the object in it. `probe-pause.sh` 1244 frames after the reload shows:
- `Hu3DPauseF 1`, `HuSprPauseF 0`
- 0 of 10 processes paused (they are new)
- the new board's key object with `work[0]` 0

So the board's models stand still. The resumed turn never gets past its first wait on a motion, and from frame 3764
to 17400 there is no further turn point.

Clearing `Hu3DPauseF` right after the reload (`unpause3d.sh`) is enough: the run plays the turn, the minigame and the
results like a turn-1 resume. Audio pause state was not read.

This is exactly N76: "MatchStartProc ... never lifts HuPrcAllPause/Hu3DPauseSet/HuSprPauseSet/audio pauses". N76 is
open and owns that fix, so I did not touch `MatchStartProc`. Its check can be `cases.txt`'s three `*-pause-*` lines:
they must reach a minigame without `unpause3d.sh`.

Without a reload, the forced pause is never left either: mash-a's A taps do not close the pause menu, and the board is
still paused at frame 5000. So in a scripted run, the pause case can only be judged against the turn point and the
unpause probe, not the end hash.

## Tried / failed

- **A FORCE breakpoint at the same function as EVENT never ran.** That was `force-land.sh` with
  `EVENT=BoardSpaceLandExec`. When two breakpoints share a location, gdb runs the first one's commands, and its
  `continue` skips the rest. The first happening runs were plain land reloads. They were rerun with
  `EVENT=HuAudFXPlay if $landset==1` (the happening's sound right after the forced land). `force-dumpall.sh` misses
  the turn point that `reload.sh`'s own breakpoint takes, for the same reason, so its tp1 is turn 1 player 1.
- **Last-5-turns in turn 1 cannot be resumed.** There is no turn checkpoint before it ("cannot continue: a checkpoint
  of 0 bytes"). Hence turn 2 with `max_turn` 6, and `start` mode for turn 1.
- **Battle/Bowser minigames with mash-a never came back.** mash-a taps START for instDll only at frames 10500–10900,
  tuned for the turn-1 minigame:
  - The battle's instDll waited until then, and its minigame did not end by 17400.
  - With four COMs (`force-allcom.sh`, so instDll leaves on its own), the battle returns at about 12100.
  - Bowser's m435 (four COMs) sat in a `HuWinProc` message wait from about 6100 to 17400 (`probe-procs.sh`). m436
    never ended with COMs or with mash-a. m437 with mash-a ends and returns at 8013, so the return cases force m437.
- **Stars:** mash-a never passes the star on w02..w06 by frame 17400 (the first `*-star` runs never hit
  `BoardStarExec`). Hence `force-star.sh`.

## Other checks

- **Offline unchanged:** `--script tools/netplay/mash-a.script --turbo --hash-log` with this branch gives 17400
  frames, ending on `AE36457C2B93C6C0`. That is N66's and N72's value. The new reset lines run only from the netplay
  scrub.
- `tools/check.sh` passes (run by `done`).

## Not verified

- **Board pause reload playing on without help:** it does not, and that is N76's fix (above). The audio pauses after
  such a reload were not inspected.
- **Two peers:** every case here is one process. N66's two-peer `--desync-at` runs cannot be aimed at these events
  without gdb on both peers.
- **m435/m436 returns:** the Bowser minigame return was checked only through m437, because the other two never ended
  in a scripted run. That they hang with four COMs or mash-a may be a scripted-input artefact; it was not looked into
  (out of scope).
- **The forced happening on w02..w06:** an arbitrary space turned into a happening space runs the board's
  `landEventFunc` on a space it was not placed for. Each board's own happening spaces, with their space-specific
  branches, were not reached.
- **Bowser space events other than the minigame** (shuffle, revolution, suit give), and Koopa Kid's: these came only
  where the random roll took them in the plain `*-bowser` runs, which reloaded 200 frames in.
- **A reload while the Bowser suit is on, then its restore on the new board:** not reachable from a turn-point resume.
  The fix covers it by the rule.
- **A rejoin (`start` mode)** was run only for the turn-1 last-5-turns event.
- **Windows/Apple/Android:** not built (no SDKs). The change is plain C under `TARGET_PC`.
