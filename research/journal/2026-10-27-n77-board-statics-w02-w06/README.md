# N77: board statics audit on w02–w06 (N68 did w01)

N68 dumped libdol's statics in a guest that played a match before and in a fresh host, at the same frames of the
next match, and checked each board static that still differed after the board's init. It ran w01 only. This task
runs the same harness on w02–w06, across boards (match 1 on one board, match 2 on another), and reviews the three
things N68 left open: w06's Bowser minigame path (flag 1,6 and the `itemPrev` restore), `boardSpaceStarTbl` on a
board with fewer than 8 star spaces, and each board REL's own init.

## Outcome

One more desync of this kind was found and fixed:

- **`player.c` `itemPrev` at w06's start.** The board's opening (start.c `ExecStart`) ends with
  `BoardStarShowNext(-1)`, the first star's show. On w06 that runs `boardStarShowNextHook` (the REL's `fn_1_CD0C`:
  Bowser's spot moves, a `BoardSpaceStarGetRandom` draw (BoardRand), space type 10, a message window) unless
  `BoardItemPrevGet() == 9` (Chomp Call: the star moved by an item this turn). No turn has run yet, so `itemPrev` is
  what the last board session in this process left: 0 in a fresh process, -1 after any completed move, and **9 if
  the last session was left after a Chomp Call and before that move ended** (`BoardPlayerTurnMoveExec` sets it back
  to -1 at the move's end; leaving the match, or the Bowser minigame that a Chomp-Call turn walked into, skips that).
  That peer skips Bowser's spot and the BoardRand draw, the others do not.
  Fix: start.c sets `itemPrev = -1` right before that `BoardStarShowNext(-1)` when `netplay_active()`. -1 is what
  every peer that finished a move has; 0 (fresh) and -1 take the same branches everywhere it is read before the
  first turn writes it (the turn start sets -1 anyway). Offline is unchanged (the GameCube cannot leave a party
  mid-turn without a reset, so its value there is always 0 or -1).

Everything else that differs after the board's init on w02–w06 is written before it is read (below).

## Harness (`harness/`)

N68's (`research/journal/2026-10-27-n68-board-statics/harness/instrument.diff`, minus its `mg_setup.c` hunk, which is
in main now), with the N56 button presser copied to `build/n77/n56_harness.inc` and one addition there: the host
sets the Room's board from `PB_N77_BOARD` (`pbnet_lobby_settings`) before it starts the match.

- `rejoin.sh <tag>`: N68's rejoin. `BOARD` (0..5) is match 2's board (host B), `BOARD1` match 1's (host A, default
  `BOARD`). Runs in `build/n77/run/<tag>`.
- `staticdiff.py`: N68's, unchanged.
- `pairdiff.sh <run>`: staticdiff of every guest match-2 dump against host B's dump at the same frame and overlay,
  `src/game` symbols only, minus N68's always-different list (REL function pointers, engine bookkeeping).
- `converge.py <pairdiff.txt>`: per `board/` static that differs at frame 300 (after the board's init), the last dump
  where it still differs and the first where it is equal.
- `symval.py <libdol.so> <dump> <symbol> [fmt]`: one static's value in a dump.
- `lastsession.py <hash-log> <out>`: the last session of a hash log (the guest's log has match 1 and 2).

Scratch only (not committed): `PB_N77_ITEMPREV=<n>` sets `itemPrev` when `--leave-at` fires (as if left after a
Chomp Call), `PB_N77_NOFIX` turns the fix off.

## Runs (GMPE01 Rev 1, `$PB_DISC`, relwithdebinfo, Linux, private netns per run, relay off, vsync off, `setarch -R`)

Board numbers are the Room's (0 = w01 ... 5 = w06). "Identical" is `tools/netplay/hashdiff.py` on the guest's match-2
session against host B's, over their common frames; `heap=` differs from frame 0 in every run (N74: layout, not state).

| run | match 1 → match 2 | result |
|---|---|---|
| same1..5 | w02→w02, w03→w03, w04→w04, w05→w05, w06→w06; match 1 to frame 11000 (turn 1, a minigame, results), match 2 320 s | **identical for 18344 / 18383 / 18322 / 18384 / 18382 frames**: two turns and two minigames each (ovl 27, 9 on w02; ovl 39, 38 on w03; ovl 21, 45 on w04; ovl 14, 18 on w05; ovl 15, 19 on w06), no desync. Built before the fix |
| x0to5 | w01 → w06 | identical for 18704 frames (ovl 14, 10), no desync |
| x5to1 | w06 → w02 | identical for 18699 frames (ovl 11) |
| x3to4 | w04 → w05 | identical for 18701 frames (ovl 33, 17) |
| **nofix** | w01 (left at 1500, guest's `itemPrev` forced to 9 at the leave) → w06, fix off | host B and the guest first differ at **frame 2717** (`brand`, `sys`: the first star's show, host B draws Bowser's spot and the guest does not); pbnet's desync at 2730. The resync restarts the match at the board start, the guest's `itemPrev` is still 9, and it desyncs again at 2730: **4 desyncs in 240 s**, the match never gets past 2730 |
| **fix** | the same, fix on | **identical for 13891 frames**: the opening, turn 1, a minigame (ovl 14), results, turn 2, into the next minigame (ovl 16) |

The forced 9 stands for the real route (a Chomp Call, then leaving before the move ends): the read in start.c sees
the same value either way.

## What still differs after the board's init (frame 300), and why it is harmless

`converge.py` over every run. The N68 table (research/journal/2026-10-27-n68-board-statics) covers most of these:
roll.c, com_path.c, view.c, mg_setup.c (`hilitePos` fixed in N68), shop.c, ui.c's picker/`itemUsed`, `moveAwayPlayer`,
`junctionArrowRot`, start.c's `playerOrderNew` (N56) / `playerOrderOld`, window.c's `winComKeyBuf`. They converge
where N68 said. New here:

| static | verdict |
|---|---|
| `player.c` `itemPrev` | **read before written on w06** by the board start's `BoardStarShowNext(-1)` (fixed, above). Elsewhere it is read only after `BoardPlayerTurnExec` sets -1, or (Bowser's minigame return, below) after `fn_1_AB4C` restores it |
| `space.c` `spaceCnt[1]` | stays 108 (w01's layer-1 coaster spaces) after a w01 match, 0 in a fresh process; never converges on w02–w06. Layer 1 is only touched by w01's REL, whose init reads it (`BoardSpaceRead(1, ...)`) first; libdol only loops over layer 0 (`BoardSpaceCountGet(0)`). Not read |
| board `window.c` `messSpeed`, `winPause`, `winAttr`, `choiceDisableTbl`, `winPosIdx`, `defaultChoice`, `disablePlayer`, `winPortrait`, `winInsertMesTbl` | all set by `BoardWinCreate`/`BoardWinCreateChoice` before the window's process reads them |
| `pause.c` `cursorPos` | set from the menu's work when the pause menu opens, before use |
| `mushroom.c` `itemResult` | set at the mushroom event's start (line 383) before the box reads it |
| `last5.c` `focusPos` | set from the space position before the model uses it |
| `ui.c` `boardItemUser`, `itemPickupPos`, `itemPlayer`; `item.c` `currItemRestore`; `com_path.c` `startPathSpace` | set at the pick (`boardItemUser` with `itemUsed`), at `itemPlayer` = the picker's player, at `BoardItemStart`, memset at every search |
| w05's `boardObjMan`, `boardMainProc`, `cameraObj`, `tauntObj`, `modelMgrObj`, `modelDataList`, `statusProc`, `spaceTexData`, `playerMatCopy` (never equal) | heap pointers: both non-null, a constant 0x333FE000 apart. In that run the guest's heap sat at another base. Pointer-only |

### w06's Bowser minigame path (flag 1,6)

Before the minigame, bowser.c (REL) saves `itemPrev + 1` in `board_data` (`GWSystem`, in the board blob), sets flag
(1,6), and calls the minigame. The board comes back with (1,6) set: `BoardStartCheck` is true, so no start sequence
(the start.c read above does not run). `ExecTurnStart` calls `boardTurnStartFunc` (w06's `fn_1_9294`) →
`fn_1_93C8` → `fn_1_AB4C`, which sets `itemPrev` back from `board_data` before anything reads it. Then
`BoardPlayerTurnExec` skips its `itemPrev = -1` (flag 1,6) and `BoardPlayerTurnMoveExec` reads it at the move's end,
after the restore. A resync or an N52 continue into m457/m458 brings `board_data` with the blob, so every peer
restores the same value. No stale read on this path. Not run two-peer (no Bowser space was landed on in these runs).

### `boardSpaceStarTbl` on a board with fewer than 8 star spaces

`BoardSpaceRead` fills the table from the board's star spaces and never clears it, so a board with fewer than 8 would
keep the last board's entries, and `BoardSpaceStarGetNext`'s `BoardRandMod(8)` retry loop reads all 8. But every
board the Room offers has 8 (frame-300 dumps): w01 (N68), w02 `85 36 27 59 51 75 91 80`, w03 `53 128 95 109 118 70 81
87`, w04 `32 10 49 39 56 19 65 73`, w05 `4 67 72 80 31 41 11 56`, w06 `12 61 47 42 39 96 78 68`. The table only differs
before the board's `BoardSpaceInit` (frame 1 dumps), never after. The boards that may have fewer (w10 tutorial,
w20/w21 extra boards: `BOARD_ID_EXTRA*` skip the star platform) cannot be picked online (`NETPLAY_BOARDS` 6, the
lobby's `BOARDS`), and an offline session on one of them is followed by a main board's full refill.

### Each REL's own init

- REL statics: overlays are `dlclose`d. A running guest that played w01 in match 1 and w06 in match 2 mapped only
  `w06Dll.so` (`/proc/<pid>/maps`), so a REL's statics start from the file at each load on Linux/glibc.
- libdol state a REL sets: every hook a board REL installs is cleared at the board start before the REL's
  `BoardCreate` runs: `boardTurnFunc`, `boardStarShowNextHook`, `boardBowserHook`, `boardStarGiveHook`,
  `boardTurnStartFunc` (`BoardObjectSetup`), the walk/land event funcs and junction mask (`BoardSpaceInit`, space.c's
  kill), pre/post turn hooks (player init), the light hooks and create/destroy (main.c), the camera's pos calc func
  (`BoardCameraInit`: memset and `pos_calc = NULL`). So a board that does not set one (w05 sets no junction mask, only w01 the camera
  calc, only w06 the five main.c hooks) never sees the last board's. The REL-pointer values that differ in the dumps
  (N68's list) are the same function at another load address.
- libdol code with board-specific branches (`GWBoardGet() == BOARD_ID_MAIN*`: bowser.c, boo_house.c, lottery.c,
  space.c, star.c, roll.c, last5.c, start.c, view.c, main.c) reads only synced state (`GWSystem`, flags, the hooks
  above), except star.c's `itemPrev` reads (above).

## Offline

`harness/offline.sh` (`--script tools/netplay/mash-a.script --turbo --hash-log`, own user dirs; a w01 party, and the
same with `--match 5@300` for w06) with and without the fix: **identical for 17400 frames each, heap included**. The
mash-a run ends at `DD93480BCB38595C`, as in N68. A `--script` run has netplay active, so the guard runs; a fresh
process has `itemPrev` 0, so -1 changes nothing.

`tools/check.sh` passes (run by `done`).

## Tried / failed

- `pkill -f <pattern>` in my wait loops matched the shell running it (N68 hit the same): the loop died with exit 144.
  Nothing else was lost; I reran the commands without it.
- staticdiff needs the libdol.so the dumps came from (symbol addresses move with a rebuild), so the same-board runs'
  diffs used a copy (`build/n77/libdol-same.so`) and the rebuild waited until they were done.

## Not verified (follow-up: N80 for the Bowser minigame return)

- **A real Chomp Call before a leave.** The nofix/fix runs force `itemPrev = 9` at the leave (scratch
  `PB_N77_ITEMPREV`). Driving a CPU/human to buy and use a Chomp Call and then leave mid-move needs a scripted item
  route the harness does not have. The read in start.c is the same either way.
- **w06's Bowser minigame return, two-peer.** Reviewed in code (above); no run landed on Bowser's space, and no
  resync was forced into m457/m458.
- **Leaving mid-event** (shop, Boo, lottery, battle, Bowser's minigame itself) on w02–w06: the leaves here were at
  fixed frames (11000 and 1500), not inside an event. N66's subject; not run per board.
- **The extra boards (w10, w20, w21)**: not online boards (the Room has 6), so not audited as a match board. Only
  their effect as an earlier offline session was reasoned about (`boardSpaceStarTbl` refill).
- **Statics only later turns reach** (the last 5 turns, shop visits and item use on every board, each REL's own
  event code, e.g. w04's bridge.c, w06's fire.c): the runs covered two turns per board. Their libdol side was reviewed by the pattern above, their REL side is reloaded fresh.
- Other platforms (Windows, Android: whether a REL unload resets its statics there), more than two peers, a real
  network: not run.
