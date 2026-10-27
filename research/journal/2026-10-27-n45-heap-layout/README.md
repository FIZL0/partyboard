# N45: lobby heap layouts from board frame 4

## Outcome

**The frame-4 difference was in N26's layout hash, not in the heaps.** N26's lobby peers had different hashes from
board frame 4 on because the hash took each block's `num` raw. From that frame, `num` is a host pointer for the
board's model blocks:
`hsfman.c:406` `Hu3DModelCreate` sets `mallocNo = (uintptr_t)hsf`, and `ObjConstantMake`, `Hu3DObjDuplicate`,
`SetObjAttrMotion` (hsfmotion.c:1050), `MakeDisplayList` and `Hu3DModelLink` allocate with that num. `hsf` is an
address in the DATA heap, and the heap's base moves with each process's ASLR. N15 (cd00d98d,
`heap_num()` in `src/port/netplay_match.c`) already changed the hash to (heap, offset) for such nums. N26's
branch was cut before cd00d98d (`git merge-base --is-ancestor cd00d98d 44ade807` is false), so its runs used the
raw hash.

Nothing to fix in the code. The heaps of two peers that start a match from the same place are laid out the same
on every frame:

| run | heap layout (current hash) |
|-----|----------------------------|
| N35 lobby (`build/n35run`, host/guest, from frame 0 of the match) | identical, 41179 frames |
| N38 runs 1-3, epoch 0/1 (first match, both from the Online menu) | identical, 7260 / 5668 / 5950 frames |
| N45: two parallel `mash-a.script` runs (fresh user dirs, boot to w01, m420, w01 again) | identical, 17400 frames |

## Can peers run out of heap differently?

**First match: no.** The allocator is first-fit and the layouts are the same. The same requests therefore succeed
or fail the same way on both peers.

**After a rejoin reload, the layouts differ, but not by enough to matter.** In the N38 logs, every reload epoch's
heap hash differs from frame 0 to the end of the epoch (up to 18423 frames). The state hash stays identical
through those epochs. The peer that was mid-match keeps blocks that the freshly started peer does not have
(`netplay: heap N:` lines at the board start):

| peer at a reload's board start | SYSTEM | DATA | DVD |
|---|---|---|---|
| fresh (rejoined guest) | 7 blocks, 43968 B | 44, 13184 B | 0 |
| mid-match host, N38 run 2, reloads 1-4 | 15, 103456 B | 76 / 118 / 156 / 198 blocks, 16416 → 40832 B | 678016 B once, then 0 |
| N38 run 3 host, reload 1 | 15, 103456 B | 134, 29792 B | 2165056 B |

Headroom, measured with the instrument below over the `mash-a.script` path (boot, party setup, w01, m420,
results, w01). PC heaps are 4x the GameCube sizes (`src/game/malloc.c`):

| heap | size | peak used (where) | smallest "largest free block" seen |
|------|------|-------------------|------------------------------------|
| SYSTEM | 9.0 MB | 0.52 MB (w01) | 8.45 MB |
| MUSIC | 5.0 MB | 1.25 MB | 3.75 MB |
| DATA | 42.0 MB | 15.18 MB (w01, frame 10228) | 26.82 MB |
| DVD | 22.0 MB | 3.83 MB (party setup) | 18.17 MB |

The leftovers are at most 60 KB (SYSTEM), 41 KB (DATA) and 2.1 MB (DVD). The smallest contiguous free space on
this path is 8.4 MB, 26.8 MB and 18.2 MB. Different OOM results on two peers would need a scene within those few
KB/MB of the heap's limit, and this path is nowhere near that. Game code never branches on free memory: the only
readers of `HuMemHeapSizeGet`/`HuMemUsedMallocSizeGet` are debug prints (`m402` etc., objmain) and
`board/model.c` after a failed read. Any difference would show up only as an allocation that fails on one peer.
That allocation would log `HuMem>memory alloc error` and dump the heap, so it would be visible.

One thing does grow. The mid-match peer's DATA heap gains about 40 blocks (~8 KB) on every reload. At that rate it
would take thousands of reloads to use up the 26.8 MB. It is filed as **N57**: find the callers and free them at
the match start, so reload layouts match as well.

Note: 64-bit costs heap. DATA peaks at 15.2 MB on w01, more than the whole GameCube DATA heap (10.5 MB), because
blocks have 64-byte headers and the structs hold 8-byte pointers. So the 4x heaps do not give 4x
the GameCube's margin: on this path DATA has 2.8x its peak. Only w01 and m420 were measured.

## How it was found

1. Read the old logs before running anything. `build/n38run/run{1,2,3}` and `build/n35run` are logs from earlier
   tasks in this slot. They were split per session with N38's `split.py`, and the hash-log `heap=` column was
   compared host vs guest. The first match was identical, and only the reloads differed. So the current build did
   not show N26's frame-4 difference at all.
2. `git log -S heap_num` dated N15's fix to 13:16. N26 was committed at 13:39 on a branch without it.
3. Instrument (`instrument.diff`, not committed):
   - `HuMemDirectMallocNum` stores `__builtin_return_address(0)` in the block's `retaddr`.
   - The hash log gets `heapraw=` (the old raw-num hash), `used=`, `peak=` and `minfree=` per heap.
   - With `N45_DUMP=a-b`, every block whose num is a heap pointer is printed with its caller (`dladdr`).

   Two runs went in parallel with fresh user dirs and vsync off:
   `N45_DUMP=4180-4200 partyboard --script tools/netplay/mash-a.script --hash-log runN.log`.
4. Results:
   - `heap=` was identical on all 17400 frames.
   - `heapraw=` differed on 17179 frames, first from the boot (bootDll's models), then again from w01's frame 2.
     w01 starts at 4182, and the first differing frame is 4184. In N26's lobby run, the board started at frame 2
     and the difference started at frame 4: the same two-frame offset.
   - At 4184 the dumps list 1084 blocks with pointer nums: 353 `ObjConstantMake`, 25 `SetObjAttrMotion`, and 706
     from static functions (no `dladdr` name; `MakeDisplayList` and friends). Their offsets and sizes are equal
     in both runs. Only the num differs: `7f5d356abdb0` in one run and `7f6c556abdb0` in the other, which is
     `82000000000acda0` (DATA + 0xacda0) in both.

## Tried / failed

- The instrument's `heapraw=` column lags one frame. `n45_raw_hash` is a printf argument, and it was evaluated
  before `netplay_heap_hash()` (which recomputes it) in the same call. The block dumps (printed before the hash)
  gave the right frame: 4184. Fix the argument order if the diff is reused.
- My first wait-for-exit loop used `pgrep -f` with a pattern that matched the loop's own shell, so it never
  ended. It was killed by hand.

## Not verified

- **A new two-peer lobby run.** None was made for this task. The lobby results above come from the N35/N38 logs,
  which were made on earlier builds that already had `heap_num` (with raw nums, peers with models loaded could
  not have matched for 41179 frames). The cause was confirmed with two single-process
  `--script` runs, which differ in ASLR just like two peers. That is the whole mechanism, but it is not a lobby
  run.
- **N26's exact build.** Its tree was not rebuilt to show the raw hash diverging there. It is inferred from the
  missing cd00d98d and from the same offset (board start + 2) in the instrumented runs.
- **Headroom on other boards and minigames.** Only w01 and m420 were measured. A minigame that comes close to a
  heap limit would shrink the margin. That is most likely in DATA, given the 64-bit growth.
- **Which code leaves the reload leftovers.** Only their sizes are known (from N38's heap lines). The callers are
  N57's job.
- **Windows/Android/Apple.** Linux only. Heap sizes are the same on every 64-bit PC target, but ASLR and the
  allocator base differ. `heap_num` covers that.
