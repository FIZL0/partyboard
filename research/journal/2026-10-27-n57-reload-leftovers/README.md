# N57: what a reload leaves in the heaps

## Outcome

A peer that was mid-board or mid-minigame when a reload came kept heap blocks that a fresh peer does not have. All
the callers were found and dealt with. With the fixes, the heaps at a reload's board (or instDll) start match a
fresh peer's, block for block (`netplay: ... heaps 3E1CFDACE4558E3D` at every reload start of the runs with the
fixes in: r2, j1, r6, r7, j2). DATA no longer grows from reload to reload. Two peers that both reloaded stay identical,
heap layout included, through the next epoch (r2, r6, r7). After a rejoin (one peer fresh, the host mid-match), the
state stays identical, but the layout differed inside one overlay per run (N69, below).

| # | leftover (N45/N38 numbers) | caller | fix |
|---|---|---|---|
| 1 | SYSTEM +8 blocks, 59 KB | `chrman.c` `EffectInit`: `particleData[]`, allocated on the first `CharModelCreate` and kept for the whole run | `CharEffectNetplayReset` frees them (effect models are gone by then) |
| 2 | DATA +2 blocks per window, ever (the "+~40 blocks per reload" growth) | PC `HuSprAnimMake` allocates `bank->frame` and `pat->layer` as blocks of their own; `HuSprAnimKill` never freed them. Every window (`HuWinCreate` → `winBGMake`) leaked 2 blocks, offline too | `HuSprAnimKill` frees them for a made anim (`raw == NULL`) |
| 3 | DVD 678 KB / 2.1 MB once | the old board's async directory reads (`CreateBoard`'s `BoardDataDirReadAsync(DATADIR_BGUEST)`; instDll/BoardMGSetup's minigame dir). `DestroyBoard` closes them, but a killed board never runs it | `HuDataNetplayCloseAll` closes every finished read; also `HuARDirFree(DATADIR_BOARD)` as N34's leave does |
| 4 | DATA, a minigame left mid-play (sprite anims) | PC `HuSprAnimRead` converts into new blocks with no num. On GC the anim *is* the file block and goes with `omOvlKill`'s `HuMemDirectFreeNum(MEMORY_DEFAULT_NUM)` | the converted blocks take the file block's num (`HuMemDirectNumGet`) |
| 5 | DATA, m411 left mid-play: 5 blocks per anim | `Hu3DAnimKill` frees only the ANIMDATA block (GC: the whole anim) | `HuSprAnimFree` frees the PC anim's own blocks (not if `HuSprAnimKill` already freed it) |
| 6 | DATA, m428/m439: 36 KB | `Hu3DShadowCreate`'s buffer; the minigames put their own buffers in `Hu3DShadowData.buf`, so `Hu3DAllKill` freed theirs and the original was lost (offline too on PC) | tagged `MEMORY_DEFAULT_NUM`: `omOvlKill` frees it |
| 7 | DATA, m427 (2 × 600 KB screen copies), m440 (objects), m415 (made anims) | the minigame's own `HuMemDirectMalloc` blocks, freed only at its normal end | at the reload's scrub, every block whose allocating code is no longer loaded is freed (below) |

### Blocks of code that is gone

On PC `HuMemDirectMalloc*` now keeps its caller (`__builtin_return_address(0)`, `HU_MEM_CALLER`) in the block
header's `retaddr`. It used to be 0 on PC: GC used it for the debug heap dump only, and it is not in the heap hash.
`HuSprAnimMake` records its own caller for its six blocks.

`leftovers_free` runs in `netplay_ovl_start`, at the scrub. By then the old overlay's code is unloaded
(`pcOvlToKill`), and the new overlay is not loaded yet. Any block whose caller `dladdr` (or `GetModuleHandleEx`) cannot
place in a loaded module belongs to the dead overlay, and `HuMemMemoryFreeIf` frees it. The sentinels
`0xCDCDCDCD`/`0xA5A5A5A5` and 0 are never freed, and nothing is freed on a platform where `code_module` cannot
resolve libdol itself. A false positive would need persistent code to hold a pointer to a block that an unloaded
overlay allocated. A fresh peer at that point has only libdol blocks (boot: `game_main`, `Hu3DInit`, `MGSeqInit`,
audio, card).

### Why at the board's start and not in MatchStartProc

The brief said "free them at MatchStartProc". But MatchStartProc runs in frame 0, before the old board's processes
are torn down (N38). Freeing chrman's particle work or the directory data there would pull memory from under code
that still runs in that frame, and a minigame's REL is still loaded then. `netplay_ovl_start`'s scrub is the first
point where nothing of the old overlay exists. It is also where N38 already resets the state and N52 the dice.

## Tests

- `tools/netplay/match_test.c`: the board's start calls `CharEffectNetplayReset`, `HuDataNetplayCloseAll` and the
  ARAM free. A block whose caller is not loaded code is freed, and one of loaded code stays. Checked by mutation:
  with the "gone" block's caller set to loaded code, the test fails.
- `tools/check.sh` (see the end).

### Two peers (GMPE01, private netns, relay off, N38/N56 harness; scratch, `harness/`)

| run | what | result |
|---|---|---|
| r1 (before the fixes) | guest `--desync-at 4000`, resync to player 1's turn point | reload board start: SYSTEM 15/103456 B, DATA 72, DVD 678016 B vs 7 / 44 / 0 fresh. The instrument named the callers (above) |
| r2 | same, fixes 1–3 | all four board starts `heaps 3E1CFDACE4558E3D` (SYSTEM 7, DATA 40, DVD 0). Epoch 1 identical 13438 frames, heap included, through 2 minigames |
| j1 | **N38's rejoin**: guest SIGKILLed at player 1's turn point, restarted, *Rejoin Match*. The host reloads mid-board, the guest is fresh | both reload board starts `3E1CFDACE4558E3D`. Epoch 1 state identical 17650 frames; heap identical on frames 1–16903 and 17314–17650, see below |
| r3, r4 | guest `--desync-at 8500` (inside the round-1 minigame, ovl 30 / 42), fixes 1–3 | the instDll resume start: DATA 44 / 52 vs 40, one and three converted sprite anims of the killed minigame (→ fix 4) |
| r5 | same, + anim num | m411 (ovl 19): DATA 80 (its eight `HuSprAnimMake` anims, 5 blocks each, → fix 5) |
| r6 | same, + `HuSprAnimFree` | m411 again: instDll resume start `3E1CFDACE4558E3D`. Epoch 1 identical 8487 frames, heap included on every frame |
| r7 | final code (+ N56 harness only), `--desync-at 8500` | m408 killed mid-play; both instDll resume starts `3E1CFDACE4558E3D`. Epoch 1 identical **19767 frames**, heap on every frame, through 3 rounds (m408, m415, m440) |
| j2 | final code, rejoin at player 2's turn point | both reload board starts `3E1CFDACE4558E3D`. State identical 17656 frames; heap identical except inside m401 (frames 1790–5516), see below |

### One process: every minigame killed mid-play (`harness/sweep.sh`)

A temporary bootDll hook jumped straight into minigame overlay N, with 4 COMs. A hook in `netplay_tick_begin`
restarted the overlay 600 frames in, twice, the way a reload kills it (the overlay's processes are killed, then
`omOvlGotoEx`). The heaps' block counts were printed at each overlay start. Overlays 9–68 (m401–m462):

- Before fixes 6–7: m415 +6 DATA blocks per kill, m427 +2 (600 KB), m428/m439 +1 (36 KB), m440 +1. Everything else
  was unchanged (SYSTEM 7 → 15 once: chrman's particle work, which offline keeps by design).
- With `N57_LEFTOVERS` (the netplay scrub's `leftovers_free` called at each overlay start): all 58 minigames that
  start this way have SYSTEM 7 / DATA 40 / DVD 0 blocks at every start, the same as a fresh peer. The dead-code rule
  freed blocks for m415 (6), m427 (2) and m440 (1) only. It never freed anything at a first start (from bootDll).
  `memory free error` counts were the same as without the change (m428 1 per kill, overlay 52 8; N70).
- m420 and m425 crash in their setup when started this way (all groups 0; m420 is N52's 1-vs-3 case). This is not
  related to the kills: they crashed before the first one.

### Offline unchanged

`--script tools/netplay/mash-a.script --turbo --hash-log` (boot, party, w01, m420, results, w01; 17400 frames, own
user dir) ends at `004C982C415C8583`, N43/N52's value, with the final build (`build/n57run/off2`). The state hash is identical
on all 17400 frames to a run with only the first fixes (`off1`). The heap layout differs from frame 1911 (ovl 70,
party setup), as it should: windows no longer leak. The one `memory free error` (after bootDll) is in both runs.

m411 played straight from boot for 9000 frames (`N57_MG=19`), with and without the sprman changes: same final hash
(`2720AB7F480845B4`), and the same eight `HuMem>memory free error` lines at its end in both.

## Found on the way (not fixed here)

- m411's own end frees its made anims with `HuSprAnimKill`, then `omOvlKill`'s `Hu3DAllKill` frees them again
  (`Hu3DAnimKill`): eight `memory free error` lines on every play, before this task too. `HuSprAnimFree` keeps
  that case as it was: it sees that the anim is not a used block, and it does not touch the sub-blocks.
- r5 (m411 resumed after a reload) logged one more `memory free error` inside the minigame, at `d1d9d9f0`. It did not
  happen in r6 (also m411 after a reload) or in the one-process kills. Not chased.
- **After a rejoin, the heap layout (not the state) can differ inside one overlay** (N69). j1: round 3's resultDll
  (frames 16904–17313), after two rounds with identical layouts. j2: all of m401 (1790–5516), the first minigame
  after the reload. Both peers' `HuMemHeapDump`s at that overlay's start and end are identical (addresses masked),
  and the layouts match again at the next overlay. It never happened between two fresh peers (epoch 0 of r2–r7,
  each through round 1 and its minigame) or between two reloaded peers (r2, r6, r7: 13438 / 8487 / 19767 frames).
  So it is tied to one peer having played before. A libdol `if (!static) alloc` that keeps a pointer from the old
  match on the host would look like this, and it would also be a use-after-free. Not chased here.

## How it was found

- The instrument (`harness/instrument.diff`, on top of N56's `harness/instrument.diff`, not committed):
  - every `HuMemMemoryAlloc2` records a 16-frame `backtrace()` in a side table keyed by the data pointer;
  - with `N57_DUMP`, the scrub's heap walk (and, offline, every overlay start) prints each used block with its
    trace; `harness/sym.py <out> <builddir>` groups them by `addr2line`'d caller chain;
  - `N57_MG=<ovl>` (bootDll, with `skipBootSequence`) jumps straight into that overlay with 4 COMs;
    `N57_RESTART=<ovl>:<frames>:<times>` restarts it mid-play as a reload kills it; `N57_LEFTOVERS` runs the
    netplay scrub's `leftovers_free` at each overlay start; objmain prints the heaps' block counts there.
- `harness/pair.sh` (N56's, with `N57_DUMP`, run dir `build/n57run/<tag>`; `GARGS="--desync-at 8500"` puts the
  resync inside round 1's minigame), `harness/rejoin.sh <tag> <player>` (new: N38's rejoin: the guest is
  SIGKILLed at that player's turn point and comes back with *Rejoin Match*; `harness/n57_harness.inc` is N56's
  presser with a `rejoin` role; point `online.cpp`'s include at it), `harness/sweep.sh <ovl>...` (the one-process
  kills). Split hash logs with N38's `split.py`, compare with `tools/netplay/hashdiff.py`.

## Tried / failed

- Traces from coroutine stacks stop at the REL frame (no name, `WindowSmallD0Ev` is a wrong nearest symbol), so
  minigame callers were found from the libdol frame below them and the block sizes.
- My first `rejoin.sh` ended with a bare `wait`, which also waits for the netns holder (`sleep 100000`), so it never
  returned. The committed copy waits for the two peers only.
- The first sweep showed m415/m427/m428/m439/m440. Fixing them one by one in the RELs would have been 5+ REL edits
  that are not `netplay_active()` guards. The dead-code rule covers them, and any other minigame of the same kind,
  in one place.

## Not verified

- **The dead-code rule in a two-peer reload.** It was exercised through the same `leftovers_free` at each overlay
  start in one process (the sweep), and by `match_test`. The two-peer runs after it landed (r7, j2) killed m408 and
  a board, and neither had blocks of that kind. Which minigame round 1 plays cannot be chosen.
- **m420 and m425 killed mid-play.** They crash in setup when jumped into with groups 0 (a harness limit), so the
  sweep never killed them. Whether a 1-vs-3 or 2-vs-2 minigame leaves anything different was not checked.
- **Board events killed mid-way** (shop, lottery, Boo, battle, the last-5 event, Bowser). The board reloads were from
  turn points: start of a turn or mid-roll (r1/r2/j1/j2). The dead-code rule and the directory close cover the
  classes found, but other board-only statics like chrman's (allocated once, kept in a dol static) would not be
  caught. They would show up as a SYSTEM/DATA count above 7/40 at a reload's start (`netplay: heap N:` lines).
- **Other boards than w01.** Every run was board 0.
- **The heap-layout difference inside an overlay after a rejoin** (N69, above).
- **Windows / Android / Apple.** Linux only. `HU_MEM_CALLER` needs GCC/Clang. Elsewhere it is 0 and nothing is
  freed by the rule. `code_module` uses `GetModuleHandleEx` on Windows, untested here. Android (bionic `dladdr`) and
  Apple (`dladdr`) take the same path as Linux.

## Gate

`tools/check.sh` passes on the final tree (cargo tests 75 passed / 2 ignored, `match_test` with the N57 block, the
CMake build).
