# N66: board statics left by a reload mid-turn

**Outcome: fixed.**
- A reload (resync, rejoin, CPU stand-in) in the middle of a board event left the old board's handles in dol statics.
  These are models, sprites, windows, objects and processes.
- One root process (item.c's `ItemRotProc`) even kept running.
- On the old code, I reproduced 2 crashes, 1 hang (3 events), and a process left alive that keeps moving a model of the
  new board.
- With the fix, every reload I forced reaches the next turn point with board statics like a fresh board's, heaps like
  a fresh peer's, and plays on.
- Two-peer `--desync-at` runs at three such moments stay identical for 19000–25000 frames after the resync.

## Cause

`MatchStartProc` replaces the board in frame 0 of a reload. N62 runs the board processes' destructors first. But much
of the board's teardown happens later:
- **An object's own func, a frame or more after its kill.** The func sees `BoardIsKill()` or its kill flag, then
  clears its static (`filterObj = NULL; omDelObjEx(...)`).
- **DestroyBoard's kills**, which a reload never runs (`BoardModelKillAll`, `BoardSpaceDestroy`, ...).

`omOvlKill` then frees the objects, models and every `MEMORY_DEFAULT_NUM` block, but the statics still name them. The
dol statics outlive the overlay; only the REL is dlclosed. A fresh peer has their initial values.

## Fix

**Statics.** Each `src/game/board/*.c` file with such statics gets a `Board*NetplayReset` at its end (`TARGET_PC`). It
works like N52's `BoardRollNetplayReset`: every static that names a model, motion, sprite (group), window, effect,
particle manager, anim, object or process goes back to its declaration value. So do the event state flags tied to
them:
- lottery `coinF` and `handUpdateF`, block `coinF`
- player `megaDoubleDiceF`
- view's flags, boo `booKillF`
- com `itemUse`: a COM's item choice from the old board

**Also reset in `main.c`:**
- `boardObjMan`, `boardMainObj`, `boardMainProc`
- the old REL's `createFunc`/`destroyFunc` and light hooks

**Also in `space.c`:**
- the three space event hooks
- the two space `GXTexObj`s, destroyed as `BoardSpaceDestroy` does (otherwise each reload leaked two aurora textures)

`netplay_match.c` `board_statics_reset()` calls them all in `netplay_ovl_start`'s scrub, where roll's reset was. That
is before the new overlay's ObjectSetup (`objmain.c:97`), and nothing of the old board runs any more. Files: basic_space,
battle, block, boo, boo_house, bowser, char_wheel, com, fortune, item, last5, lottery, main, mg_setup, model,
mushroom, pause, player, shop, space, star, start, ui, view, warp, window. That is 26, plus roll.

**ItemRotProc.** It is `HuPrcCreate`d as a root process, not a child of `boardObjMan`, by the Boo bell, the genie lamp
and the item bag. Only its item kills it. It is dol code, so `MatchStartProc`'s overlay kill keeps it, and `omOvlKill`
does not reach it either. `BoardItemNetplayKill()` (`HuPrcKillIf` on its func) kills it in `MatchStartProc`, after
N62's destructors. The log line is `netplay: match start: killed N item processes`. It is the only root process in
`src/game/board`; every other one descends from `boardObjMan` (grep `HuPrcCreate(`, `HuPrcChildCreate` parents).

**Left as they are, by design:**
- **Plain values that every event sets before use:** positions, current player, choices, `spaceData`, `boardCamera`,
  com_path's search tables.
- **The tutorial's statics:** w10 is no netplay board.
- **mg_setup's minigame history:** `netplay_match.c` already clears it at a match start (`BoardMGSetupPlayClear`).

**Rule for new code:** a new board static that names an object goes into its file's reset. The rule is in
`plan/NET.md`.

## What the reloads left, and what it did

One process, `harness/reload.sh`: N62's gdb approach (mash-a, `--match 0@900 --turbo`). A board function's first call
schedules a reload N frames later. `start` mode is `netplay_match_start`; `cont` mode is `netplay_match_continue` with
the last turn blob, the resync path.

`harness/dump.py` dumps every static of `src/game/board/*.c` (445 symbols, `harness/syms.txt`). It does this at each
board start, before (`pre`) and after (`start`) the scrub, and at the first turn point after it (`turn`). Events the
script never reaches are forced from gdb:
- `force-land.sh`: the landed space's type
- `force-walk.sh`: a passed space gets the Boo house or lottery flag
- `force-item.sh`: player 0 becomes a COM holding item X, and `BoardComUseItemCheck` answers yes

Stale handles at the reload, beyond the ones every board leaves (host models, shop/lottery/Boo house models,
`modelDataList`, the space textures, `cameraObj`/`tauntObj`, `playerMatCopy`, `uiStatus`, all set again by the board's
create). Output of `stale.py`:

| reload in | event-specific statics left |
|---|---|
| roll / walk (`move`), shop, Boo house, lottery | player `rollObj` |
| mushroom space, happening (w01's event lands on a mushroom box) | mushroom `boxObj[2]` |
| blue, red space | basic_space `coinChgObj[0]` |
| mg_setup roulette | main `filterObj`, mg_setup `mgSetupObj` |
| Bowser space | main `filterObj`, bowser `miniBowserObj` |
| battle space | main `filterObj`, battle `battleObj`, `explodeObj` |
| warp | char_wheel `charWheelObj`, `wheelSprGrp` |
| Boo house | boo `booEventObj`, boo_house `ballMdl`, `houseMdl[1..5]` |
| lottery | lottery `ballMdl`/`gripMdl`/`lotteryMdl`/`lotteryMot`, player `motDoneF[4]` |
| Boo bell (11) | **ItemRotProc alive**, boo `booEventObj`, item `booBallMdl`/`suitMdl`, com `itemUse` |
| genie lamp (12) | **ItemRotProc alive**, item `genieParticleAnim`, com `itemUse` |
| item bag (13) | **ItemRotProc alive**, main `filterObj`, ui `itemPickObj`, com `itemUse` |
| board intro, star, fortune (reload inside the fortune minigame, ovl 52) | none beyond the common ones |

**Old behaviour.** Same cases in `cont` mode, with all of N66 undone from gdb (`harness/skip-all.gdb`: each reset and
the kill `return`):
- **mg_setup, Bowser, battle: the board never leaves again.** The next `BoardFilterFadeInit` sees the old `filterObj`,
  writes `kill = 1` into freed memory and loops `while (filterObj)` for good. Shown with `skip-main-reset.gdb`: called
  at frame 14032 with the old pointer, and no instDll by 17400. The same hang is how I first found it (the first
  `start`-mode mg_setup run, before any fix).
- **Item bag: SIGSEGV** in `TeamItemPosSet` (ui.c:1192) <- `BoardItemStatusKill` <- `BoardPlayerItemAdd` <-
  `DoItemGive` (mushroom.c:510), through the old `itemPickObj`.
- **Genie lamp: SIGABRT**, malloc's heap check in Dawn's queue submit: heap corruption after the reload.
- **Boo bell:** no crash. The heaps at the board start differ from a fresh peer's (`0E0717544DEC6598` vs
  `BDDB26B214B20C84`): the left ItemRotProc's stack is still allocated. `skip-item-kill.gdb` shows it after the reload,
  setting model 0 of the new board for the rest of the run (2400+ times by frame 12664).
- **The other 11 cases** ended on the same hash as the fixed build: in these runs nothing read their stale values
  before the board set them again. They are still reset, because a later event would read them. Examples: the next
  coin counter's free-slot search skips a stale `coinChgObj` slot and writes through it. `BoardCharWheelCheck` answers
  "still running" while `charWheelObj` is set, a wait like the filter's.

**With the fix, all 17 cases, in both modes:**
- no crash
- the board start's heaps are `BDDB26B214B20C84`, a fresh board's
- `cont` resumes the right player's turn, then minigame, results and board again
- in `start` mode, `diff.py` against the fresh run's first turn point shows **no pointer or handle difference**. Only
  input-driven values differ: mash-a's taps fall on other frames after a reload, so the intro's turn order differs.
- 15 of the 17 `cont` runs end on the same hash, `D91EE5439FF5E594`. All 15 resume a turn-1 point and then play the
  same. The other two: the shop reload comes at frame 15117 in turn 2, and the star run resumes at player 1.

## Two peers, `--desync-at` (GMPE01, private netns, relay off)

`harness/pair.sh` is N57's pair script with its own run dir. The scratch harness was applied for the runs and reverted:
- N38's `harness/netplay_c.diff` (pads)
- N57's `harness/n57_harness.inc` in `online.cpp` (lobby presser)
- a one-line `OSReport` at the start of 7 board event functions, to find the frames

All runs: board 0, 20 turns, 2 humans + 2 CPUs. A calibration pair gave the event frames, but the pairs that followed
took other paths (N53), so the resyncs landed in these events:

| run | guest | resync during | after it |
|---|---|---|---|
| d-mgs | `--desync-at 6100` | mg_setup, 479 frames after it started (still on the board, 3 destructors run) | both resume player 3's turn; mg_setup, minigame 12, turn 2 (shop, ...). **Epoch 1 identical 22158 frames** |
| d-item | `--desync-at 9230` | a star event, 193 frames in (`BoardStarExec`) | both resume player 0's turn 2; turn 2, mg_setup. **Identical 19044 frames** |
| d-land | `--desync-at 3060` | player 0's walk (turn 1) | both resume player 0; turns 1–2, minigame 13. **Identical 25220 frames** |

The guest's log is 2 lines longer each time: it was killed later. Every board start logged heaps `3E1CFDACE4558E3D`
(N57's fresh value).

## Other checks

- `tools/netplay/match_test.c`: stubs for the 26 resets (one bit each) and `BoardItemNetplayKill`.
  - A board start that is not the one waited for runs none.
  - The scrub runs each of them once.
  - The item kill runs in `MatchStartProc` before `omOvlCallEx`.
- **Offline unchanged:** `--script tools/netplay/mash-a.script --turbo --hash-log` with this branch, twice, and with the
  pre-task sources (`git checkout b4d5906f -- src/game/board src/port/netplay_match.c`, same build dir). All three logs
  are **byte-identical** (17400 frames, end `AE36457C2B93C6C0`; main moved since N52's `004C982C...`). By
  construction: the resets run only from `netplay_ovl_start`'s scrub and `MatchStartProc`, both netplay-only.
- `tools/check.sh` passes.

## Tried / failed

- `force-item.sh` first forced `ChooseUseItem`'s return. It is inlined (gdb: "Can not force return from an inlined
  function"), so it forces `BoardComUseItemCheck` now.
- The hammer (item 4) does not start ItemRotProc. Only items 11–13 do (`ItemShowProc` with work).
- The first fixed runs used a binary built before the `main.c`/`space.c` additions (`boardObjMan`, hooks). Rebuilt and
  run again; the tables are from the second runs.
- The two-peer runs could not be aimed: the calibration pair's item use (frame 9188) became a star event in the d-item
  pair.

## Not verified

- **Two peers during an item, the lottery, the Boo house, a shop, Bowser or battle.** The lockstep pads (A every 24
  frames) never brought those up at the desync frames, and they cannot be forced across two processes without gdb on
  both. They were checked in one process through the same reload calls (`cont` = the resync's
  `netplay_match_continue`), on both the fixed and the old code.
- **The items not forced:** mini/mega (also super), hammer, pipe, swap, spark, whistle, Bowser suit. Their statics are
  in the resets (item, player, ui), but no reload was run in them. Neither was the last-5-turns event (last5.c), the
  board pause screen (pause.c) or a battle/Bowser minigame's return to the board.
- **Boards w02..w06.** Only w01 was run. Their REL events are killed as overlay processes (N62) and their REL statics
  are fresh after `dlclose`, but the dol helpers they call were only reached from w01.
- **A rejoin** (N38/N57 path) was not run. It goes through the same scrub.
- **The GPU side of the texture destroy:** that the two `GXDestroyTexObj` calls at the scrub free aurora's textures.
  There was no texture count to read; the call is the same one `BoardSpaceDestroy` makes.
- Windows/Apple/Android not built (no SDKs). The code is plain C under `TARGET_PC`.
- The reload checks not run above are filed as N76.
