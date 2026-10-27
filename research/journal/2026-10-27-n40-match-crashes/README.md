# N40: the crashes that kept a --match party from its end

N25 saw two crashes in a scripted `--match` party: a segfault in the board at ~6973 when START was tapped, and
`*** stack smashing detected ***` in turn 2 (~16077). Both are found and fixed. A third crash at the very end of
the party (heap corruption in the results screen's save) is fixed too. With the three fixes a scripted party plays
all 10 turns, and a continue from the turn-10 checkpoint reaches `netplay: match over` and modeseldll. A fourth
crash (GPU device loss in m419) and nondeterminism between identical runs were found and filed, not fixed.

## 1. START in the board: HEAP_DATA runs out (sprite file blocks leak on PC)

`gdb`: SIGSEGV in `HuSprAnimRead(data=0x0)` <- `BoardSpriteCreate` <- `CreatePauseScreen` (pause.c:293). Just before
it, `HuMem>memory alloc error` and a dump of heap 2 (HEAP_DATA, 42 MB): 0x4e60 bytes free. The dump has 135 copies
each of blocks sized 0x8320/0x2be0/0x9100/0x2ae0, one set per START tap (the script tapped START every 40 frames).

Cause: on GC, `HuSprAnimRead(data)` converts the file block in place and returns it as the `ANIMDATA`, and
`HuSprAnimKill` frees that block. On PC (BYTESWAPPING) it allocates a new `ANIMDATA` (+ bank/pat/bmp) and leaves the
file block alone; frames, layers and bitmaps still point into it. `HuSprAnimKill` freed the copies but never the
file block. Every sprite killed before its overlay ends leaked its whole file (the pause screen ~128 KB per open). The
blocks have MEMORY_DEFAULT_NUM, so an overlay's end freed them; inside one board instance they piled up.

Fix (`include/game/animdata.h`, `src/game/sprman.c`):
- `ANIMDATA.raw` (BYTESWAPPING only): the file block `HuSprAnimRead` converted. `HuSprAnimKill` frees it where GC
  frees the anim. `HuSprAnimMake` sets it to NULL.
- GC returns the same anim when the same file block is read twice (its offsets are pointers by then, useNum++).
  The PC code made a second anim and swapped the frames back. Freeing `raw` would then free it twice. So after
  converting, `HuSprAnimRead` marks the file block (its 20-byte header is no longer used): 0xD3D3D3D3 where the bank
  offset was, then the anim pointer. A second read of that block returns the same anim with useNum++, as on GC.
  Every `HuData*Read*` returns a fresh heap block, so a marked block is never shared. Static data (hsfman's
  reflection maps) is writable and never killed on PC.

Check: the alternating A/START script (START every 40 frames from 1500) runs to its end (10000) with no alloc error.
It crashed at ~6973 before.

## 2. Stack smashing in turn 2: w01's CoasterPathUpdate

On this tree the pads from N25 take another path (N26's reseed changed the seeds), and no crash came. So I built
N25's tree (fbe4cbb9, `git archive` into the scratchpad, same submodules) and ran its setup: mash-a up to 1500, A
every 20 frames, explanations off (temporary `GWMGExplainSet(0)`), turbo. In gdb it never crashed. Without gdb 2 of
3 runs aborted with the stack smashing message at frame 16722 (turn 2, the board). Their turn-1 checkpoint was at
12501, not 12461 as in gdb: gdb changes the run's path (see "Runs are not deterministic" below). The core
(`coredumpctl dump`) shows:

```
__stack_chk_fail <- CoasterPathUpdate (w01Dll/main.c:764) <- CoasterLandExec <- CoasterLandMain
```

`BoardSpaceLinkTargetListGet` (space.c) always clears `BOARD_SPACE_LINKMAX` (4) s16, but `CoasterPathUpdate`
passed `s16 linkTbl[3]`: 2 bytes past the array. On GC that lands in padding. On x86-64 with GCC's stack protector it
can land on the canary. The other callers have room (w01 `CoasterPlayerLoopCheck` [4], w04/w05 [4], player.c [10]
with at most 4+4 entries).

Fix (`src/REL/w01Dll/main.c`): under TARGET_PC `linkTbl[BOARD_SPACE_LINKMAX]`; GC keeps `[3]` so the decomp still
matches. Check on N25's tree with the same fix: 6 runs without gdb, none aborts at 16722 any more (all pass it).

## 3. The party's end: mstory3Dll's save overflows its process stack

The board after turn 10 goes to mstory3Dll (ovl 78), which restarts itself for the party results (event 1). There
`fn_1_16924` waits for the result player's pad: **A turns the page, START (PAD_BUTTON_MENU) leaves**. With A taps
alone a script stays there forever. That is the game, not a bug. With A and START alternating every 20 frames,
START gets lost in A's page animation, so a script should tap START alone there.

Leaving runs `fn_1_1712C` (result.c:481, created with stack 0x2000, 16 KB on PC). It saves (`SLSave` ->
`HuCardCreate` -> aurora card -> SDL -> fseek). The process then aborted: `malloc(): invalid next size (unsorted)`.
In gdb it aborted in another malloc, so the glibc heap was corrupted earlier. libco stacks are malloc blocks, so an
overflowing coroutine writes into the chunk below. Checks:
- With every stack 4x bigger (temporary): no abort, `match over` and modeseldll come up.
- Temporary instrumentation: paint each stack with 0xA5 at create and report the high-water mark at process end and
  every 500 frames. In the turn-10 continue only one process used more than 3/4 of its PC stack: `fn_1_1712C`, 20264
  bytes of 16384. In a whole 10-turn party (all its minigames and the first results screen) no process did.

Fix (`src/game/process.c`, `HuPrcCreate`, TARGET_PC): after the existing `stack_size *= 2`, at least 64 KB. I
picked this over a bigger stack for this one process: at least nine DLLs reach the save/card code
(`mgmodedll`, `mstoryDll`, `mstory2Dll`, `modeseldll`, `option`, `resultDll`, `mpexDll`, `safDll`, `mstory3Dll`),
and many processes have GC budgets of 0x2000, 2000 or 1000 bytes. The stacks are malloc'ed outside the HuMem heaps,
so the game's heaps and the state hash do not change. The turn-10 continue ends with the same hash
(BFFBFBEDB66FA8FF) with the floor as with 4x stacks. Cost: 64 KB of malloc per live process.

## In-game results (GMPE01, own user dir per run, vsync off, temporary turbo)

- `--match 0@1500`, A every 20 frames, explanations off: all 10 turns, 13 minigames, 10 checkpoints (turn 10/10 at
  115361), no crash up to 150000. That build had fixes 1 and 2 only; the run ended on the results screen (A only).
- `--continue <the turn-10 checkpoint> --match 0@900` from the title, A taps, START from 6000: board turn 10/10 at
  901, mstory3 (78) twice, `netplay: match over: overlay 74 starts at frame 6193`, `match over at frame 6194`,
  modeseldll (74), then mentDll (70). The online save file is gone afterwards (N31), and MemoryCardA.USA.raw was
  written (the party's own save).
- One run from 1500 to the end in one go: not reached. It hit crash 4 (m419) in turn 8.

Temporary changes during the work, all reverted: `PB_TURBO_TMP` (disableFrameLimiter), `PB_EXPLAIN0_TMP`
(GWMGExplainSet(0) at match start), toggles for the leak fix and N26's menu-process kill, 4x stacks, the stack
painter, a m419 probe.

## Tried and dropped

- N26's menu-process kill turned off on this tree: no crash, but the end hash changes. The leftover modeseldll
  processes do run. This was not the stack smash.
- The leak fix turned off on this tree with A-only pads: no crash in 30000 frames. The stack smash does not depend on
  the leak.
- ASan: the CMake option is Windows-only, and libco stack switching would give false reports. The core dump and the
  stack painter were enough.

## Also seen (not N40, not fixed)

- **Crash 4 (N52): m419 (ovl 27) loses the GPU** 4 frames after it starts: `radv: GPUVM fault detected`,
  `VK_ERROR_DEVICE_LOST`, aurora FATAL. It happens in runs alone too. radv's "this context is innocent" is
  misleading here. It also killed parallel runs at the same moment. m419 draws with `GXSetZTexture(GX_ZT_REPLACE,
  GX_TF_Z24X8)` and copies Z24X8 with `GXCopyTex` (fn_1_848/fn_1_AD0), likely the culprit. I could not confirm it:
  the probe run took another path (below). Filed.
- **Runs are not deterministic (N53)**: the same binary with the same pads, two runs in parallel: `hashdiff.py` first
  differing frame 25497 (plr2), heap layout from 25097, both in m420 (ovl 28). N25's tree diverges too (turn-1
  checkpoint 12461 vs 12501), and gdb changes the path. Two peers would desync there. Filed.
- `HuMem>memory free error`, harmless (the magic check catches them), same on GC's code:
  - bootDll `NintendoDataDecode` (main.c:1099) frees a pointer that is not a block start.
  - mstory3 win_effect.c:181 `HuSprAnimKill`s anims that `Hu3DModelKill` of their particle model already killed:
    5 errors per anim (bank, pat, bmp, raw, anim). The two kills run back to back, so nothing is reallocated
    between them.
  - `HuSprGrpKill` keeps the freed `members` pointer. A second kill of the group (here HuWinAllKill at mstory3's
    end) frees it again.
- m444 (ovl 52) went on for 12-22k frames with A taps in two runs (state changing, GW system hash constant); not
  looked into.

## Not verified

- **A whole party from the match start to `match over` in one run**: the end was reached from the turn-10
  checkpoint, and turns 1-10 in another run. One run through both hit m419 in turn 8 (filed).
- **Two peers** (lockstep, N42): not run; this was all `--script` single-process.
- **Offline play**: these are general port bugs (the leak, w01, the save stack), so offline parties had them too.
  Offline play was not run. The fixes do not touch netplay paths.
- **The pause menu's own behaviour** (A inside it): only checked that opening it no longer runs the heap dry.
- **The stack floor's memory cost** in a long session: not measured (64 KB x live processes, malloc).
- **Fix 2 on this tree**: the stack smash never came up here (its pads take another path), so the check is the
  6 runs on N25's tree with the same fix.
- **Unit tests**: none added. The leak and the overflows need the game's sprite/process code with real data; the
  checks are the in-game runs above.
- Windows/Android/Apple: not built (no toolchains; Android not tried).
