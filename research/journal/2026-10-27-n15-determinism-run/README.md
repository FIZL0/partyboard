# N15: N4 determinism check against a real disc

## Outcome

With the GMPE01 Rev 1 image at `$PB_DISC`, two runs of `tools/netplay/mash-a.script` with `--hash-log`
give **byte-identical logs**. The run goes boot → modesel → party setup (mentDll) → the w01 board
(BoardRandInit reseeds `brand`, board turns are played) → minigame instructions (instDll, frame 10579).
Both runs were 13200 frames, final hash `49E98D41456DA884`, `cmp` equal, `heap=` included.

**Not reached: the minigame itself.** The script's START window ends before instDll accepts START.
A run with a wider window was killed by the session's low-memory reaper at frame 7152 (its log matched
the two runs' logs frame for frame up to there). Filed as **N34**. The committed script is exactly the
one the two identical runs used.

No state diverged between two runs that start from the same user dir state. Two things did differ, and
both are explained below: the heap-layout column (a host pointer, fixed) and the menu path when the user
dir already holds a memory card (an input, not nondeterminism).

## How to run it

```
B=build/linux-default-relwithdebinfo/partyboard
for n in 1 2; do
  rm -rf /tmp/pb$n; mkdir -p /tmp/pb$n          # a FRESH user dir per run: no MemoryCardA.USA.raw
  XDG_DATA_HOME=/tmp/pb$n HOME=/tmp/pb$n timeout -s KILL 1800 \
    $B --script tools/netplay/mash-a.script --hash-log run$n.log >run$n.out 2>&1 &
done; wait
tools/netplay/hashdiff.py run1.log run2.log
```
The game runs windowed at roughly 10-25 fps here (two at once, other slots busy), so 14000 frames take
10-20 minutes. `timeout -s KILL` and output to a file: the game ignores SIGTERM.

Overlay numbers in the `ovl=` column (US build, `include/ovl_table.h`, TARGET_PC + non-JP branch):
1 bootDll, 3 instDll, 8-69 minigames (m401 = 8), 70 mentDll, 74 modeseldll, 84 resultDll, 89-94 w01-w06.

## What was done

- **Script tuned** (`tools/netplay/mash-a.py` writes `mash-a.script`; the frames are constants at its top):
  - All four ports tap A. Party setup (mentDll, `main.c:1022`) waits for each player to press A on
    their own port. With only port 0 tapping, the old script sat in character select forever (a
    24000-frame run never left overlay 70). All four ports are connected and human, so on the board
    every player rolls with A too.
  - Port 0 taps START alone in a window around instDll's start (10500-10800). instDll (`main.c:295`) leaves only when
    the OR of the human ports' pressed buttons is exactly START, and only after its intro (`instMode == 1`).
    Four ports tapping A never get out (a run sat in instDll from frame 10443 to 24000).
    The window turned out to be too early (see Outcome, N34). Outside the window START is not
    pressed after the menus, because it pauses a minigame.
- **Heap layout hash fixed** (`src/port/netplay_match.c`, `heap_num`). The `heap=` column differed
  between every pair of runs from frame 33 (boot) on. I dumped every heap's block list at frame 34 in two
  runs (temporary debug code, not committed): offsets, sizes and used flags were identical. Only `num`
  differed, and those values were host pointers (`0x7f47391a6ef0`...). `hsfman.c:406` tags a model's
  blocks with `(uintptr_t)hsf`, an address inside a game heap, and the heaps' base moves with ASLR. A `num`
  that points into a game heap is now hashed as (heap, offset). After the fix, `heap=` matched too.
  `tools/netplay/match_test.c` gained a check (two heaps at different addresses, a block tagged with an
  in-heap pointer, same hash); it aborts without the fix and passes with it.
  This matters for N8: its "heaps laid out alike" log line would otherwise always disagree between peers.

## Findings

- **The memory card changes the menu path.** The first run into a user dir creates
  `MemoryCardA.USA.raw`. A later run into the same dir finds the card, and from frame 1122 (in modesel)
  GWSystem, GWPlayer and GWPlayerCfg differ: mentDll starts at frame 1791 instead of 1911, and every
  later frame shifts. Two runs that start from the same card state are identical. So this is an input
  (the save), not nondeterminism, but it means:
  - the script's frames assume a fresh user dir (no card);
  - peers' saves differ in real netplay; N8's match start overwrites the state the board reads, which is
    the intended answer. A lockstep through the *menus* would diverge on this.
- RNG coverage in the run: `frand` changes thousands of times (menus, board, minigame), `rand8` 4 times,
  `brand` is reseeded at frame 4064/4184 (first w01 frame + 2, BoardRandInit) and advances through the turns.

## Tried / failed

- Old `mash-a.script` (port 0 only), 3600 frames: identical across two runs, but it stops in party setup.
- Port 0 only, 15000 frames: never left mentDll. A screenshot (grim) showed character select with 1P-4P
  filled; reading mentDll showed each port must confirm. I stopped taking screenshots after that one: the
  window is on the user's desktop and other grabs captured unrelated windows.
- All ports A, 24000 frames: reached w01, then sat in instDll (needs START alone).
- START taps at 10500-10800: too early, instDll's intro was not over (from a fresh dir it starts at 10579).
- START taps at 10620-12000, 14000 frames: killed by the session's memory reaper at frame 7152 (the machine
  was low on memory with other slots running games). Not restarted: the reaper says not to. N34.
- First build of the heap fix broke the gate: match_test links netplay_match.c against stubs that lacked
  `HuMemHeapSizeGet`. Added the stub and the test above.

## Verified

- Two runs of the committed `mash-a.script`, in parallel, fresh user dirs: 13200 lines each, `cmp`-identical,
  `hashdiff.py` "identical: 13200 frames", overlays `1@1, 74@995, 70@1911, 89@4182, 3@10579`.
- The earlier 3600-frame script: two runs identical (before the heap fix, only `heap=` differed).
- A 400-frame pair after the heap fix: identical including `heap=` (no heap note).
- Heap dump at frame 34 of two runs: identical offsets/sizes/flags, only pointer-valued `num` differed.
- `brand` reseeded at the first w01 frames and advancing; `frand` advancing through menus and board.
- `tools/check.sh` passes; match_test passes, and fails with the fix reverted.

## Not verified

- **The minigame part of the task: not reached** (N34, see Outcome). So `frand` inside a minigame and the
  return to the board (the second BoardRandInit) were not checked for determinism.
- Other platforms (Windows/macOS/Android): `--script` pads are only injected where `--wrap` works (N14).
- Runs from a user dir that already has a memory card: identical to each other (t1 and t3 matched up to
  their shared length), but the script's frames are tuned for the fresh-dir path only.
- Determinism across *different machines* or builds (compiler, float code generation): only same binary,
  same machine was run.
- The N4 open questions not touched by this run (`VIGetRetraceCount` stub, audio stubs, `sreset.c`
  threads) did not cause a divergence within 13200 frames; longer play (several turns, more
  minigames, the match end) was not run.
