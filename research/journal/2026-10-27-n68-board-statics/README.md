# N68: board/overlay statics read before written in a new board session

N56 found that `start.c`'s `playerOrderNew` kept the last board start's rolls, so a peer that had played a match
before desynced against a fresh one. This task audits the other statics that carry over like that.

## Outcome

One more desync of this kind was found and fixed:

- **`mg_setup.c` `hilitePos`** (the roulette cursor). The minigame roulette (`UpdateMGList`) sets it to 0 when it
  starts and leaves it where it stopped. The lucky roulette (`UpdateLuckyValue`, the x2/x3 coin bonus, 5% of minigames:
  `BoardRandMod(100) < 5` in `MGSetupProc`) runs before the minigame roulette, and it does *not* reset it. It spins
  until the cursor lands on the lucky value it already drew, so the number of frames it spins depends on where
  `hilitePos` starts. A fresh process starts at 0. A process that ran a roulette before (an earlier match, or an
  earlier minigame before a reload) starts where that roulette stopped. The board then goes on some frames later on
  that peer, and pads and BoardRand draws fall on other frames.
  Fix: `UpdateLuckyValue` case 0 sets `hilitePos = 0` when `netplay_active()`, before it draws the lucky value. Every
  peer then spins as a fresh process does, at every lucky roulette. That covers an earlier match, a reload at a turn
  point and a reload into instDll alike. A reset at the board start would miss the last two.
  Offline is unchanged: the decomp (and the GameCube) keep the old value in one process.

Nothing else in the audit is read before it is written in a new session (below). Two peers with N56's rejoin (a
guest's second match against a fresh host) stayed identical through two turns and two minigames.

## How the audit was done

### 1. Dumps of libdol's statics (`harness/instrument.diff`, scratch, reverted)

`n68_dump` (in `netplay_match.c`, scratch) writes libdol's writable PT_LOAD segments (`.data`/`.bss`, through
`dl_iterate_phdr`) to a file at every overlay start, and every 1000 frames (frame % 1000 == 300) while a match runs.
`PB_N68_DUMP=<dir>` turns it on. Both peers run under `setarch -R` (no ASLR), so libdol sits at the same address in
every process and the dumps compare byte for byte.
`harness/staticdiff.py <libdol.so> <a.bin> <b.bin> <objdir>` maps every differing byte to the libdol symbol
around it (`nm -S`). Given `CMakeFiles/dol.dir/src/game`, it lists only symbols that `src/game` objects define, tagged
with the file.

`harness/rejoin.sh <tag>` is N56's rejoin with the dumps on. Host A and the guest play match 1 to `--leave-at
$LEAVE` and leave. Host A is killed, a fresh host B hosts match 2, and the guest joins it. `PB_N68_LEAVE_ONCE` (scratch)
makes `--leave-at` fire once, so the guest plays match 2 to the end of the run. `ENVX` passes more env to all peers.
This uses N56's instrument (N38 pads, the N56 button presser `n56_harness.inc`, the BoardRand trace). Runs were in
`build/n68run/<tag>`.

### 2. What differs, and when it converges

Guest (match 2) against fresh host B, same frame, w01, `src/game` symbols only:

- **L1** (match 1 to frame 11000: turn 1, m401, the results). At the board's first frame, ~70 board statics
  differ. Most are written by the board's init: `BoardSpaceInit` (memsets `spaceData`/`spaceAttr`, zeros
  `junctionMask`, refills `boardSpaceStarTbl`), `BoardCameraInit` (memsets `boardCamera`/`cameraBackup`),
  `BoardTutorialInit`, ui's `memset(uiStatus)`, `BoardAudSeqClear`, `BoardMGSetupPlayClear`, start.c's `ExecStart`, the
  w01 REL's own init (`BoardBooHouseHostSet`, junction masks, the shop/lottery models). By frame 300 these are equal.
  The rest converge where their sequence writes them: roll.c's (`numDice`, `maxRoll`, `diceValue`, `destMode`,
  `inputTimer`, `rollPlayer`) by 3300, after each player's first roll. com_path.c's by 5300, view.c's by 2300. mg_setup.c's
  (`activeMG`, `mgListAll`, `hilite*`, `cursorPos`, `hilitePos`) at the first roulette. `CZoom`, `winnerNameW`,
  `seqWorkData` and `winData` at the minigame and results. From the second minigame (frame 17050) on, nothing in
  `src/game` differs except pointer-only values.
- **L2** (match 1 to frame 30000: 3 turns, 3 minigames, left mid-turn 4). The same, plus what the shop and the items
  left: shop.c (`activeItemTbl`, `choiceEnableTbl`, `itemChoice`, `itemCurChoice`, `shopPlayer`, `shopMdlPtr`,
  `angleVal`), item.c (`currItem`, `suitMdl`), ui.c's item picker (`itemUsed`, `itemMdlId`, `picker*`, `itemPosTemp`),
  com_path.c's search tables, roll.c's `sameRollCoinStr`, window.c's `winComKeyBuf`, `CRotM`/`CZoomM`/`CenterM`. Match 2
  had no shop visit and no item use, so these still differ at frame 11300.
- Always different, and harmless: function pointers into the board REL (`walkEventFunc`, `landEventFunc`,
  `walkMiniEventFunc`, `boardLightSetHook`/`ResetHook`, `createFunc`/`destroyFunc`), because the REL loads at another
  address in a process that dlopened it before. Same function, other value. Also engine bookkeeping that the state
  never reads back: ARAM queue, DVD read status, `HeapTbl`, `VCounter`, `SLSerialNo`, `font_tex`, `mesCharCnt`, and
  the free slots of `Hu3DData`/`HuSprData`/`HuSprGrpData`/`Hu3DMotion`/`DrawObjData` (a slot is set up when it is
  taken).

### 3. Read before written? (code review of every board static in the lists, plus a pattern search)

For each board static that differed after the board's init, I checked whether a new session can read it before
writing it:

| static | verdict |
|---|---|
| `mg_setup.c` `hilitePos` | **read before written** by the lucky roulette (fixed, above) |
| `mg_setup.c` `cursorPos`, `luckyValue` | the stale value only decides whether a tick sound plays on the first frame (`if (v != cursorPos) HuAudFXPlay`); it is written before any position or choice uses it |
| `mg_setup.c` `activeMG`, `mgListAll`, `mgNext`, `mgType`, `hilite{Timer,ShowF,BlinkLen}` | written at the roulette's start (`MGSetupProc`, `DetermineMGList`, case 3 before case 4 reads them) |
| `mg_setup.c` `mgPlay*` | cleared at the board start (`BoardMGSetupPlayClear`, main.c and N38's scrub) |
| `roll.c` all | set at the top of `BoardRollExec`; `diceValue` zeroed at the roll's start |
| `com_path.c` all | memset at every path search (`PopulateCandidates`, the start-space searches) |
| `player.c` `itemPrev` | set to -1 at every turn start (`BoardPlayerTurnExec`); w06's Bowser minigame (flag 1,6) restores it from its own work |
| `player.c` `junctionArrowRot`, `moveAwayPlayer` | set when the junction / move-away starts |
| `view.c`, `ui.c` (`yourTurnSpr`, picker, `itemUsed`, `itemMdlId`), `window.c` (board) | set by their create functions; `itemUsed` on both picker exits (pick, cancel) before `ItemUseProc` reads it |
| `shop.c` | set at the shop's entry; `itemChoice`/`angleVal` only drive the item model's visibility and spin |
| `bowser.c` `playerMot`, `lottery.c`, `boo_house.c` handles | set at the event's start or by the REL's init |
| `start.c` `playerOrderNew` | N56's fix (cleared in `CreatePlayerStart`); the rest of start.c's are set in `ExecStart`/`CreatePlayerStart` |
| `window.c` `winComKeyBuf` | ring buffer read only between indices it wrote; `comKeyIdx`/`comKeyIdxNow` were equal on both peers |

The pattern search: every file-scope scalar or array in `src/game/board/*.c` that is updated from its own old value
(`+=`, `++`, `^=`, `|=`, ...): 40 statics. Each one is assigned at the start of its sequence, except `hilitePos`.
Not set at a start, but the value is only cosmetic: `fortune.c` `camTargetFortune` (camera target, fetched first),
`shop.c` `angleVal` (the item's spin), `item.c` `booBallPos` (taken from the model first), `warp.c` `warpPos` (fetched
from the space first).

## Two-peer checks (GMPE01, `$PB_DISC`, relwithdebinfo, Linux, private netns, relay off, vsync off)

| run | what | result |
|---|---|---|
| t0 | short trial: match 1 and 2 to frame 1500 | dumps line up; the board-start list above |
| **L1** | N56 rejoin, match 1 to 11000 (turn 1 + m401 + results), match 2 for 320 s | match 2 hash logs **identical for 18689 frames**: 2 turns, m428 (ovl 36), results, m406 (ovl 14). No desync |
| **L2** | match 1 to 30000 (3 turns, 3 minigames, left in turn 4), match 2 for 200 s | match 2 **identical for 11490 frames** (turn 1, m427 (ovl 35), results, into turn 2) |
| **nofix** | L1's setup, the lucky roulette forced on every peer (scratch `PB_N68_LUCKY`), fix off (`PB_N68_NOFIX`) | match 2: BoardRand first differs at **frame 6757** (board, minigame setup). The guest reaches instDll at 7068, the host at 7044 (one 24-frame A-tap period later). Same minigame (m456, ovl 62; "minigame 55" in the log), but the gameplay hash **desyncs at 8760** inside it, and pbnet resyncs everyone |
| **fix** | the same with the fix | match 2 **identical for 12697 frames**: the forced lucky roulette, the roulette, m401, results, into turn 2. No desync |

`hashdiff.py` on match 2's sessions (each log split at its frame-counter resets). The fix run's `heap=` column
differs from frame 0. That is N74 (layout, not state), and it shows in L1/L2 too.

### Offline

`--script tools/netplay/mash-a.script --turbo --hash-log` (own user dir): with and without the fix (same tree, only
mg_setup.c reverted for the baseline) **identical for 17400 frames**, ending at `DD93480BCB38595C`. (N61's
`AE36457C2B93C6C0` was before later merges; the baseline here is this branch's tree without the fix.) A
`--script` run has netplay active, so the guard runs there too. A fresh process starts at 0 either way, and this
script has no second lucky roulette.

`tools/check.sh` passes.

## Tried / failed

- My first long run was killed half-way: `pkill -f "n68run/L1"` matched the shell that ran it, so the edit after it in
  the same command never ran. Then the old `rejoin.sh` was still waiting in its loop next to the new one. I killed it
  by PID and reran L1.
- Without `setarch -R`, the dumps of two processes differ in every pointer. The first dumps were already under it.
- The full symbol list (no `objdir` filter) is mostly Rust, imgui and aurora state (pbnet's lobby, the iroh/rustls
  statics). The filter keeps it to the game's own.

## Not verified

- **Boards other than w01.** Every run was board 0. The dump tool works for any board, but w02–w06 and the extra boards
  have their own REL statics and other event code (Bowser's minigame flag 1,6 path for `itemPrev`, `boardSpaceStarTbl`
  on a board with fewer than 8 star spaces). Filed as **N76**.
- **REL (overlay) statics.** The dumps cover libdol only. RELs are `dlclose`d at their overlay's end and loaded fresh,
  so their statics start from the file each time on glibc. Not checked on Windows or Android, where the unload could
  differ (a REL kept loaded would keep its statics).
- **Statics that only a match left mid-event leaves.** L1 left in the board after the results, and L2 mid-turn. A match
  left inside a shop, Boo, lottery or battle could leave a handle (`hostMdl`, `itemMdl`, ...) that the next session
  kills or reads. This is N66's subject (reload mid-turn). Not run here.
- **The natural 5% lucky roulette.** The desync and the fix were shown with the roulette forced on every peer. The
  natural draw is the same code with the same BoardRand, so the forced runs exercise the same path.
- **Minigame-side libdol statics** (`CZoom`, `CRotM`/`CZoomM`/`CenterM`, `winnerNameW`, `seqWorkData`): they converged
  in the minigames played (match 2: m401, m406, m427, m428, m456; match 1 also m408, m418, m430). A minigame that reads them before it sets them was not
  looked for. The minigames' own code (and N69's GCC list) would show it.
- Other platforms, more than two peers, a real network: not run.
