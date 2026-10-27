# N53: two runs of the same build and pads diverge (m420)

## Outcome

**The source: m420 seeded its own RNG from a register that held a heap address.** `fn_1_198` (m420dll/main.c) does

```c
frand();
fn_1_8934();          /* include/REL/m420dll.h: void fn_1_8934(void); */
```

and `fn_1_8934` (rand.c) is `void fn_1_8934(u32 seed)`. On GC both the return value and the first argument are in
r3, so the seed is `frand()`'s result. On x86-64 the return value is in eax and the argument in edi. edi still held
what `HuPrcChildCreate` (the call before `fn_1_198`) left there, an address. The seed was therefore different
in every process (ASLR). The m420 RNG (`fn_1_8944`) only drives the CPU players (player.c: reaction timing,
`unk_84` rolls) and the team split in team mode. That explains the history:

- mash-a.script runs (all four human): m420 identical in N15/N45, since the RNG goes unused.
- N40's `--match` runs (3 CPUs): first difference in m420 (plr2, 25497); the heap layout differed earlier because
  the CPUs did different things.
- N25's tree: turn 1's checkpoint at 12461 vs 12501. With N25's seeds the turn-1 minigame was presumably m420
  (not re-run, see Not verified).
- gdb "changes the path": gdb turns ASLR off, so under gdb the address and the seed were fixed, but different from
  any normal run.

Fix (`src/REL/m420dll/main.c`, `include/REL/m420dll.h`, TARGET_PC only, so GC still matches):
`fn_1_8934(frand())`, which is what r3 carried on GC.

**A second ASLR bug, found by the check: 1 in 256 boots crashed in bootDll.** `NintendoDataDecode` (PC branch)
read its data with `GetRelIncludeData` (a HEAP_DVD block), advanced `src` past the 8-byte size/type words, and
freed `src`. `HuMemMemoryFree` then took a "header" 8 bytes into the real one. Its magic byte is byte 4 of the
real header's `prev` pointer: `0x00007fa5ea9fe010` has 0xA5 there, the in-use magic, so the free went ahead on
garbage and segfaulted (core: `HuMemMemoryFree <- HuMemDirectFree <- NintendoDataDecode <- BootExec`). With any
other address it was N40's harmless "memory free error". Fix: free `src - 2`. After the fix the boot logs no free error.

**Two uninitialized locals** that a `-Wuninitialized` pass found: mpexDll `fn_1_140B0` and ztardll `fn_1_97D0`
read `var_r28 == 0x3E7` before setting it (the GC register's leftover). PC sets it to 0 first. Neither runs in a
party.

## The check: `tools/netplay/party_check.sh`

`tools/netplay/party.py` writes `party.script`: mash-a's pads up to 1500, `--match 0@1500`, then all four ports tap
A every 20 frames. Two new script commands keep it free of frame tuning (src/port/netplay.c, `--script` runs only):

- `mg-explain off`: `party_config` sets `GWMGExplainSet(0)`, so instDll needs no START. N40 used a temporary
  patch for this.
- `ovl <ovl> <event> <port> <buttons>`: while overlay `<ovl>` runs with event `<event>` (`omovlevtno`), every press
  on that port is `<buttons>`. `ovl 78 1 <p> START` for all four ports: mstory3's party results (event 1; A turns
  pages, START leaves, N40) read the pad of the player they show, not port 0. Event 0, the ceremony before them,
  needs A.

`party_check.sh [out]` runs the script twice in parallel, each in a fresh user dir, with `--turbo`, then
`hashdiff.py`. It passes only if the logs are identical and both runs logged `netplay: match over`. It needs the
disc and a few minutes, so it is not in `tools/check.sh`. `tick_gate_test.c` has `script_ovl_checks` (the
alternation, other ports untouched, `mg-explain off`). `script_load` now frees an earlier script's events
(ASan found the leak when the test loaded two scripts). `match_test.c` stubs the new `netplay_debug_mg_explain_off`.

## Runs (GMPE01 Rev 1, `$PB_DISC`, fresh user dirs, `--turbo`, explanations off)

| what | build | result |
|---|---|---|
| `--match 0@1500`, A/20 frames, 2 runs | before fixes | identical 60000 frames (no m420 on this path any more: 62, 15, 40, 26, 35, 22) |
| same, 4 runs in parallel, 130000 frames | before fixes | all identical; 10 turns, 13 minigames, mstory3 (78) at 115507 |
| poison: new HuMem blocks and coroutine stacks filled with 0x00 vs 0xFF, 60000 frames | before fixes | identical: no uninitialized heap/stack read changes state on that path |
| m420 straight from boot, 4 COMs (temporary bootDll hook), 2 runs | before fix | seeds `ce852a70`/`20309a70`, then `b88efa70`/`5b709a70` (same low 12 bits: an address); diverge at m420 frame 3076 (plr1-3), heap from 752 |
| same, 3 runs | m420 fix | seed `f04e01f1` in all three; identical 6000 frames, m420 to its end (ovl 1 at 3348) |
| `party_check.sh`, first try | m420 fix | run 2 segfaulted at boot (bootDll, above); run 1 fine |
| `party_check.sh` with START every second tap on port 0 in ovl 78 | m420 + boot fixes | identical 135000 frames, but both stayed in the results (78 from 120025) |
| `--continue` from that run's turn-10 checkpoint, START alone on all ports from the match start | same | stuck in mstory3 event 0 (the ceremony needs A) |
| same continue, final `ovl 78 1 <p> START` | same | `match over` at 6835, then modeseldll, mentDll, a new board |
| **`party_check.sh`** (final) | all fixes | **identical 135000 frames; both `match over at frame 120835`**; w01, 13 minigames, mstory3 x2, modeseldll 120834, mentDll, w01, instDll at 129854 |

## How it was found

1. Re-ran N40's setup on this tree: no divergence. The path no longer reaches m420 (N26's reseed and later changes
   moved it), so the four long runs say only that the rest of a w01 party is deterministic.
2. Poison instrument (`instrument.diff`, env `PB_POISON_TMP`): `HuMemMemoryAlloc2` and `HuPrcCreate` (a malloc +
   `co_derive` instead of `co_create`) fill new memory with one byte. 0x00 vs 0xFF gave identical logs, which ruled
   out uninitialized heap blocks and libco stacks on that path, the brief's second guess. Its first guess, async
   DVD/data timed by the clock, cannot happen: `DVDReadAsyncPrio` and `ARQPostRequest` complete synchronously and
   the audio is stubbed (no thread).
3. Read m420 (the overlay N40 named): the argument-less `fn_1_8934()`. Then a scan for that class of bug over the
   whole tree (`argcheck.py`, `protocheck.py`, copied here): calls with fewer arguments than the definition, and
   prototypes that differ from their definition in arity or void return. All other hits are already handled under
   `#ifdef` (m404 `espEntry`, m406 `fn_1_64F8`, m447 `HuAudSeqFadeOut`), are extra arguments (option rumble,
   space.c's hook table), or are static name clashes in the scanner.
4. Every game/REL file through GCC with `-O2 -Wreturn-type -Wuninitialized -Wmaybe-uninitialized` (the game builds
   with no warnings at all). `gcc-warnings.txt` here has the maybe-uninitialized and out-of-bounds-loop lists. 32
   functions fall off their end. The ones whose value is read:
   - w03 `fn_1_12C8`, declared `s32` in main.c, defined `void` in statue.c, used in `fn_1_AF8`'s condition: it
     returns what `UpdateRollSprite` left in eax. That is the same on every run, but not what GC had in r3.
   - mstory3 `fn_1_16924` ("TODO PC return value"): START returns `HuAudFXPlay`'s value (leave) and A returns
     `fn_1_1834`'s (page). Also the same on every run.
   - m404 `fn_1_607C` (unknown pixel size) and m408 `fn_1_CE68` (unknown state) fall off only on values that
     never occur.
   The rest are board event hooks and destructors whose value nobody reads.
5. Temporary bootDll hook (`PB_MG_TMP=<ovl>` with `backend.skipBootSequence`; m420 is 1 vs 3, so groups 0/1/1/1)
   plus a seed print, to run m420 alone with 4 COMs (table above).

## Tried / failed

- `ovl` first meant "every second press is `<buttons>`" on port 0 only. A and START alternating loses START in A's page
  animation (as N40 found), and the results read the shown player's pad. START alone everywhere in 78 stalls the
  ceremony (event 0). Hence the event number, and replacing every press.

- First party script tapped START alone every 600 frames to leave the results: START opened the board's pause
  menu and A never closed it. Four runs sat on the board for 118000 frames. Hence the `ovl` command.
- `pkill -f <pattern>` matched my own shell's command line and killed it before the next command (the memory
  note warns about `pgrep -f`; same trap).
- Rebuilt while four runs used the build dir. The RELs are dlopen'ed per overlay, so later runs used snapshot
  copies (`build/n53run/bin-*`). Those four runs' RELs were only relinked, not changed in code.

## Also seen (filed, not fixed)

- `HuMem>memory free error` x20 when the first minigame (62) returns to the board, five per anim (bank, pat,
  bmp, raw, anim), like N40's mstory3 note: anims killed after their blocks were freed. Freed blocks keep magic
  205, so the check catches them on every run. If a block were reused in between, the free would hit a live block.
  In gdb (to frame 9000): all from `HuSprAnimKill` (sprman.c:463, frees at 490-497) <- `Hu3DModelKill`
  (hsfman.c:596) <- `Hu3DModelAllKill` (hsfman.c:679), 4 of them via `Hu3DAllKill` (hsfman.c:332), on model 0's
  anim.
- 143 `-Wmaybe-uninitialized` and 37 loops that index past their array (`gcc-warnings.txt`). The ones checked
  read the next struct field (m420 player.c `unk_C4[10]` is `unk_D8`; shop.c `shopMdlIdx[0][1]`), which is the
  same on every run. Not all were checked. Filed together with the w03/mstory3 return values and the double kills as **N67**.

## Not verified

- **m420 inside a party.** The check's party (seed 0x50424E54, w01) never picks m420. The fix was checked with m420
  started from boot with 4 COMs (3 identical runs, 1 vs 3 groups), not through instDll from a board.
- **N25's turn-1 divergence and N40's exact runs** were not rebuilt. That they were m420 is inferred: same
  overlay (N40), CPUs present, gdb disables ASLR.
- **Two lockstep peers.** Two processes = two ASLR layouts, as in these runs, but no lobby run was made. Peers also
  differ in other ways (N42/N64: mstory3's motion offset), which this task does not cover.
- **The other 143 + 37 warnings**: listed, a few read; filed.
- **Other boards and the remaining minigames**: the long runs covered w01 and 13 minigames. The scans cover the
  whole tree, but only for the classes above.
- **Windows/Android/Apple**: not built (no toolchains). The m420 bug is ABI-wide (arm64 has the return value and
  first argument both in x0/w0, so on Android the seed was frand()'s by accident).
- **Offline play**: not run. The fixes are TARGET_PC changes in REL/boot code and apply offline too; the new
  script commands only act in `--script` runs.
