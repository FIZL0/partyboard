# N74: the heap layout after a rejoin: a model slot's stale link tag

## Outcome

The layout difference N57 saw after a rejoin was never a different layout. It was one block's **num**, which the
heap hash (`netplay_heap_hash`, the hash log's `heap=` column) includes. Block offsets and sizes were the same on both
peers.

`Hu3DModelLink` (`src/game/hsfman.c:481`, GC code) allocates the link's HSFDATA with the slot's `mallocNoLink`
*before* it sets it:

```c
var_r31->hsf = HuMemDirectMallocNum(HEAP_DATA, sizeof(HSFDATA), var_r31->mallocNoLink);
var_r31->mallocNoLink = (uintptr_t)var_r31->hsf;
```

So the block gets whatever an earlier link in that `Hu3DData[]` slot left there: the address of that link's HSFDATA,
or 0 if the slot was never linked. `Hu3DModelKill` and `Hu3DInit` never clear it. A host that played before the
reload has the old match's values in its slots. The fresh (rejoined) guest has 0. The first time that minigame (or the
results) put a link in such a slot, the host tagged the HSFDATA with `heap_num` = DATA+0xdf9b00 and the guest tagged it
with 0. The two hashes then differed until the model was killed at the overlay's end. That fits everything N57 saw: it
lasts one whole overlay (m401 / round 3's resultDll); the `HuMemHeapDump`s at the overlay's start and end are the same
(the model doesn't exist yet at the start and is gone at the end); it happens only between a reloaded peer and a fresh
one; and the state stays identical (the num is never read for state).

Is it a use-after-free? Not in these runs. The stale num matters only if a `HuMemDirectFreeNum(HEAP_DATA, X)` runs
while our block is tagged X: another live link whose HSFDATA now sits at X, killed while ours lives. In j2's logs no
link HSFDATA was ever freed by any path except its own `HuMemDirectFree` (0 of 237 host / 236 guest links,
`harness/uaf.py`). This is GC behaviour and it also happens offline, inside one epoch: 55 of the host's 237 links
carried a stale (nonzero) tag. Filed as **N80** (not fixed here: a TARGET_PC change would alter offline heap nums).

**Fix** (netplay only): `Hu3DModelNetplayReset` (hsfman.c, TARGET_PC) zeroes `mallocNoLink` in every free slot. It
runs in N57's `leftovers_free`, at the reload scrub, where `omOvlKill`'s `Hu3DAllKill` has already freed every model.
It returns how many tags it cleared (`netplay: forgot N model slots' link tags`). Offline nothing calls it.

It also covers a case that wasn't known before: in f1 both peers' **first** board start (epoch 0, from the menus)
cleared 3 tags. The title / mode-select overlays use link models too. So two fresh peers that reached the match
through different menu paths (title vs mode select, or a rejoin from the title) could have had this difference
without any reload.

## Tests

- `tools/netplay/match_test.c`: the board's start calls `Hu3DModelNetplayReset` once, and the scrub of another
  overlay doesn't. Checked by mutation: with the call removed from `leftovers_free`, the test's assert fails.
- `tools/check.sh` passes (cargo 75 passed / 2 ignored, match_test, the CMake build).

### Two peers, N38's rejoin (GMPE01, private netns, relay off; `harness/`)

`harness/instrument.diff` (scratch, not committed) logs every `HuMemMemoryAlloc2` / `HuMemMemoryFree` while a match
runs (`N74_LOG=<file>`) as frame, overlay, heap, offset, size, `heap_num(num)` and a 12-frame module+offset caller
chain. It also carries N56's A-masher (`n38_pads`) and includes N57's `n57_harness.inc` (rejoin role).
`harness/rejoin.sh <tag> <player>` is N57's script with the log. `harness/adiff.py host.alloc guest2.alloc` finds
the first event of the last epoch that differs. Split the hash logs with N42's `split.py`, then
`tools/netplay/hashdiff.py host.s.2 guest2.s.1`.

| run | code | rejoin | result |
|---|---|---|---|
| j2 | before the fix | player 2's turn point (host frame 4608) | state identical 16722 frames; `heap=` differs on frames 9950–11218 (ovl 61, m455). The alloc logs (63107 / 63110 events from frame 3) differ in exactly one field: f9950 `Hu3DModelLink` HSFDATA, DATA+0x8104a0, num DATA+0xdf9b00 on the host, 0 on the guest (from `m455Dll stage.c:1427 fn_1_891C`). 0xdf9b00 was the host's one epoch-0 link (f4183, the board). No other difference |
| f2 | fix | player 2 (frame 4063) | state **and heap** identical on all 16097 frames after the start (frame 0 is the teardown, as in every run), through m412 (ovl 20), m426 (34), m437 (45) and two results. Alloc logs identical (48724 events). The host's epoch 0 made one link, so it had a stale tag at the reload |
| f1 | fix + the count | player 1 (frame 3631) | state and heap identical on 16617 frames (m408 = ovl 16, the results, m428 = ovl 36), alloc logs identical (31680 events). `forgot 3` at both peers' epoch-0 board start (menu links), nothing at the host's reload (no board link before frame 3631) |

## Tried / failed

- N57's `instrument.diff` and N56's no longer apply to `netplay.c` / `objmain.c` (moved code). N74's diff carries the
  pieces it needs, applied by hand.
- My first log comparison started at frame 0. Frames 0–2 are the old overlay's teardown on the host (frees nobody
  else has), so the comparison now starts at frame 3.
- The first `uaf.py` read the num field with its trailing `:`, and counted `Hu3DModelKill`'s own `HuMemDirectFree`
  (return address = the start of line 621) as a `FreeNum` hit. Fixed: a hit must come through
  `HuMemDirectFreeNum`'s return address.

## Not verified

- **The exact j2 case after the fix.** Which minigame round 1 plays can't be chosen, and f2/f1 played others.
  f2's host had a stale epoch-0 tag at the reload, but the logs don't record slot indices, so I couldn't check
  whether an epoch-1 link reused that slot. The evidence the fix works is j2's diff (the only differing field is this
  tag), the reset being in place (f1's count) and the identical logs of f2/f1.
- **j1's case (round 3's resultDll)** wasn't rerun. resultDll uses link models (`resultDll.so+89e1` kills them in j2's
  logs), so it is presumably the same tag. Not shown.
- **Different menu paths between two fresh peers** (the epoch-0 `forgot 3`): reasoned from the count, not run.
- **The use-after-free** (N80): only that it didn't happen in ~470 links. No run that forces it.
- **Windows / Android / Apple**: Linux only. The reset is plain C with no platform code.
