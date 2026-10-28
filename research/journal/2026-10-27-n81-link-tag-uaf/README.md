# N81: a link's stale HSFDATA tag (use-after-free), fixed on TARGET_PC

## Decision

**Fixed** for TARGET_PC. The fix changes no game state, only the num of one heap block per link. It removes a
use-after-free / double free that the GC code also has, and the test below shows it is reachable with nothing more
than the ordinary first-fit allocator and first-free model slot.

## The bug (GC code, `src/game/hsfman.c` `Hu3DModelLink`)

```c
var_r31->hsf = HuMemDirectMallocNum(HEAP_DATA, sizeof(HSFDATA), var_r31->mallocNoLink); /* old tag */
var_r31->mallocNoLink = (uintptr_t)var_r31->hsf;
```

The HSFDATA block is tagged with the slot's **previous** `mallocNoLink`: the address of the HSFDATA the slot's last
link had (0 if the slot was never linked). Nothing clears it (N74). Its objects (`Hu3DObjDuplicate`) are tagged with
the new address. `Hu3DModelKill` of a link does `HuMemDirectFree(hsf)` and then `HuMemDirectFreeNum(HEAP_DATA,
mallocNoLink)`. That frees **every** used block whose num is that address.

How it goes wrong (this is the test's sequence):

1. Link A goes into slot 1 with its HSFDATA at X. When A is killed, X is freed and slot 1 keeps `mallocNoLink = X`.
2. While slot 1 is busy, link B goes into slot 2. First fit puts B's HSFDATA at X. Slot 2 now has `mallocNoLink = X`.
3. Slot 1 becomes free again. Link C goes into slot 1, and its HSFDATA block is tagged **X** (stale).
4. B is killed. `HuMemDirectFreeNum(X)` frees B's objects and **C's HSFDATA**. C is still alive and drawing from
   freed memory. When C is killed later, `HuMemDirectFree` frees the block a second time, or frees whatever has been
   allocated there since.

A non-link model's kill does the same thing: `HuMemDirectFreeNum(mallocNo)` with `mallocNo` = its HSFDATA address. So
any model whose HSFDATA sits at a stale tag's address does it too. A stale tag of 0 is only safe as long as nothing
ever frees num 0.

## Fix

- `src/game/memory.c` / `include/game/memory.h` (TARGET_PC): `HuMemMemoryNumSet(ptr, num)` sets a used block's
  num.
- `Hu3DModelLink` (TARGET_PC): right after `mallocNoLink` is set, it retags the HSFDATA with its own address, the
  same tag as its objects. Nothing else can use that address as a num while the block is alive. At the link's own
  kill, `HuMemDirectFreeNum(own address)` runs after the block's `HuMemDirectFree`, as before, so it frees the same
  blocks it did before.
- I chose this over the brief's "tag 0". Tag 0 would put the HSFDATA of every link under one shared num, and any
  `HuMemDirectFreeNum(HEAP_DATA, 0)` would free all of them. A model with `mallocNo` 0 could make that call, and I
  didn't audit every caller in `src/REL`. The other option, "set `mallocNoLink` first", can't be done: the address
  isn't known before the allocation.
- N74's `Hu3DModelNetplayReset` stays. It no longer has any effect on the heap (the old value is overwritten before
  anything reads it). Removing it would mean touching netplay.c and match_test for nothing. Its comment now says so.

Effects: offline, the num of the link HSFDATA blocks changes (nothing reads it except FreeNum). Netplay: both peers
run the same code, so their `heap=` hash stays equal. The tag no longer depends on the slot's history, which makes
N74's reset unnecessary for this block.

## Tests

- `tools/hsf_link_tag_test.c` + `.sh`, run by `tools/check.sh` after the build. It links the **built `libdol.so`**
  (`--unresolved-symbols=ignore-all`, as in N49) and replaces `OSAllocFromHeap` / `OSCheckHeap` so that
  `HuMemInitAll` works without aurora's OS boot. Then it runs steps 1–4 with the real `Hu3DModelLink` /
  `Hu3DModelKill` and a source HSFDATA that has no objects. It fails (exit 2) if the setup no longer reproduces the
  address reuse, so it can't pass vacuously.
  - Without the fix (the `HuMemMemoryNumSet` line commented out, libdol rebuilt): `C ... tag 0x...61a0` (= A/B's
    address), `after B's kill, C's HSFDATA used: NO (use-after-free)`, exit 1.
  - With the fix: C is tagged with its own address, `used: yes`, exit 0.
- In game, `tools/netplay/party_check.sh` (GMPE01, two `--match` parties with the same pads, `--turbo`, fixed
  build): both runs reached `match over` (frames 116948 / 110988). The first differing frame was **78195** in ovl 48
  (m440), with `frand` first and `hash` equal. That is exactly N83's known divergence (same frame, 1856 frames into
  m440), not this change. hashdiff printed no heap note, so `heap=` (which includes block nums, and so the new link
  tags) was identical on frames 0–78194: the board, the menus' links and every minigame before m440. The game did
  not crash.

## Tried / failed

- The first scratch main called `HuMemInitAll` directly. It crashed in `OSCheckHeap(0)`: there is no OS heap
  without the game's `InitMem`. Calling `OSInit` + `OSInitAlloc` crashed too (the arena is NULL, because aurora's
  `OSInit` lives in the executable and not in libdol). Replacing the two OS heap functions in the test binary works,
  because libdol's calls to them go through the PLT.

## Not verified

- **That the use-after-free ever happened in a real game.** The test builds the sequence by hand (a stand-in keeps
  slot 1 busy). In N74's ~470 links nothing freed a link's HSFDATA except its own kill. I didn't look for a minigame
  that reaches the sequence for real.
- **Party_check after frame 78195.** N83 diverges there (m440), so the rest of the party (and the second half of
  the hash logs) wasn't compared. One pair only.
- **The netplay rejoin case (N74's j2) with the fix.** Not rerun. With the fix, the tag no longer depends on the
  slot's history, so the N74 difference can't come from this block any more. That is reasoned, not run.
- **GC behaviour**: untouched (TARGET_PC only). **Windows / Android / Apple**: Linux only. The change is plain C.
  `hsf_link_tag_test.sh` relies on ELF `--unresolved-symbols` and symbol interposition, so it is Linux-only, like
  check.sh.
