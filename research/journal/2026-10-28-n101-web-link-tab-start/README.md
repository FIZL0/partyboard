# N101: the browser match start on a tab opened by a join link

N98 run 10: four headless Chromium tabs, and the one opened by a join link (`?code=`, c) had a different state hash
at frame 30, the match's first hash, with all inputs zero; resync mended it. The brief: rerun four tabs through the
menu; if it never shows, close it (N98 found later that run 10's c had been put on d's origin, so the two shared
OPFS and the IndexedDB user dir).

## Result

- **The state desync does not come back.** In 9 completed four-tab menu runs (each tab on its own origin, c opened from the
  invite link, `--hash-log` in every tab), every tab's match session had the same state hash (and every other
  section: sys, plr0-3, cfg, frand, rand8, brand, pad, full) as the host's on every frame, from frame 0. That's 3,350 to 7,900 frames per
  run, through w01 (ovl 89), instDll, a minigame and back to the board. All 9 PASS: no desync lines and no resync
  (a 10th, run 9, lost its host tab to a 20 s stall under machine load, see the table).
  So run 10 was the shared-origin harness bug (fixed in N98), as suspected.
- **What the start kept from before it: the `heap=` column, on the link tab only.** In runs 1-6, `hashdiff.py`
  said "heap layout first differs at frame 2 (ovl 89)" for a vs c, never for a vs b or a vs d (byte-identical).
  It differed in every overlay and matched between overlays. `heap=` is not part of the state hash, so it had no effect
  on play, but it is the column that showed N92/N99's real split, and a link tab would hide the next one.
  **Fixed** in `heap_num` (src/port/netplay_match.c); in runs 7, 8 and 10, all four tabs' logs are byte-identical,
  `heap=` included (run 10: 9,359 frames through w01, instDll, minigame overlay 20, overlay 84 and back to w01).

## The heap= cause

A per-block dump of the heaps at the first overlay frame (`harness/blocks.diff`, temporary) showed 109 used blocks
on every tab, all equal except three HEAP_SYSTEM blocks (sizes 0x40, 0xc0, 0x18e0) tagged `num=8400000001d22c60` on a
and `...1d22b30` on c: `heap_num` said "heap 4 (HEAP_MISC), offset X". HEAP_MISC is never allocated from. The num
is `MEMORY_DEFAULT_NUM` (0x10000000), a constant. In the browser's 32-bit linear memory, 0x10000000 falls inside
HEAP_MISC's address range (on a, HEAP_MISC started at 0x0E2DD3A0), so `heap_num` turned the constant into an offset,
0x10000000 minus HEAP_MISC's base. That base follows aurora's `AllocMEM1` (a plain `calloc`), and so follows
whatever the page malloc'd before it. A tab opened from a join link has the long `?code=` (PB_JOIN_CODE), and its base
was 0x130 bytes higher. On 64-bit desktops heap addresses are far above 0x10000000, so this only happens in the
browser (and in any other 32-bit build).

Fix: `heap_num` returns `MEMORY_DEFAULT_NUM` and wipe.c's `0x20000000` tag as they are (the only constant tags
large enough to be in a wasm heap's range; DATA_NUM tags are < 0x01000000, `-256` is above any heap). Desktop
hashes cannot change: there those values are never inside a heap, so `heap_num` already returned them as they are.

## Also found, not fixed

HEAP_MISC's size (the arena's leftover, `HuMemInitAll`) also follows the same `calloc` address mod 32: the game
rounds the arena's ends to 32 bytes, Emscripten's dlmalloc gives 0 or 16 mod 32. A single web_check tab with
`env=PB_N101_PAD=x` of length 1/9/17/25 got 65863584 / 65863616 / 65863584 / 65863616 bytes ("HuMem> left memory
space"; every desktop log in build/ says 65863584, glibc's mmapped calloc is 16 mod 32). Link tab c had 65863616 in runs 1-3,
the others 65863584. Nothing allocates from HEAP_MISC, so only the "zeroed N free heap bytes" line shows it.
I tried an aurora patch (`AllocMEM1` over-allocates 32 bytes and places MEM1 at 16 mod 32); it made every pad
length and all four tabs 65863584 (runs 4 and 5). I dropped it, though: it does not change play, it was
not the `heap=` cause (runs 4 and 5 still had the heap= note), and an aurora patch is more to carry. If HEAP_MISC
is ever used online, bring it back.

## Tried and failed

- A single offline tab with only a different environment length (`web_check`, `--match 0@1500`, pads of 1 and 9
  chars) did not show the heap= note over 13,549 frames. I didn't work out why (the pad moved HEAP_MISC's size but
  maybe not its base by a multiple that changes the hash, or those blocks' 0x10000000 tag never landed in the range
  in that run). The four-tab run with the link tab is the repro.

## Runs

`harness/loop.sh N...` (copy of build/n101-loop.sh): `PAIR_TABS=4 PAIR_MENU=1 PAIR_ARGS='--hash-log /tmp/h.log'
DUMP_FILES=/tmp/h.log node tools/web_pair.mjs $PB_DISC build/n101-runN 120`, own WEB_PORT per run. Each tab's log
then splits (N38's `split.py`) into session 0 (title, before the start) and session 1 (the match). I compared
session 1 cut to the shortest tab's length, with `cmp` and `tools/netplay/hashdiff.py`. c's title session was
always the shortest (900-2,300 frames vs 4,700-7,100 for the others: it boots ~80 s later, from the link).

| run | build | seed | match frames compared | state | heap= |
|---|---|---|---|---|---|
| 1 | main | 18E2B9ADAB760D01 | 7753 | identical, 4 tabs | c differs in overlays |
| 2 | main | 18E2B9E8C86CBF01 | 7901 | identical | c differs |
| 3 | main | 18E2BA254620F501 | 7899 | identical (w01, instDll, ovl 62, 84, back to w01) | c differs |
| 4 | + aurora MEM1 patch | - | 7826 | identical | c still differs |
| 5 | + aurora MEM1 patch | - | 7912 | identical | c still differs |
| 6 | + blocks.diff dump | - | - | PASS | 3 blocks' num, see above |
| 7 | heap_num fix | 18E2BBB270CE0001 | 5136 | identical | **identical**, all 4 byte-identical |
| 8 | heap_num fix | 18E2BBFBB6DCFC01 | 3350 | identical | **identical**, all 4 byte-identical |
| 9 | heap_num fix, 300 s | 18E2BC5E44ACC601 | - | FAIL: host tab a answered no CDP call for 20 s at 267 s (frame ~1330, lockstep fine until then; machine load average 43-55 on 16 cores from other slots); no logs dumped | - |
| 10 | heap_num fix, 300 s | 18E2BCB51814D601 | 9359 | identical (w01, instDll, ovl 20, 84, back to w01) | **identical**, all 4 byte-identical |

`tools/check.sh` passes.

## Not verified

- **Desktop hash logs before and after the fix: not compared.** I'm relying on the argument above: on x86-64/arm64
  heaps are never at 0x10000000/0x20000000, so `heap_num` already returned those values raw. No desktop run checks it.
- **A link-tab state desync with a different cause** can't be ruled out by 9 runs (N98 had 1 in about 5 four-tab runs, with
  the shared origin). None of these 9 had one.
- **Run 9's host stall**: not investigated. It happened under heavy load from other slots, and the rerun (run 10) with the
  same build and length passed. If a four-tab host page stops answering again on a quiet machine, look at it with
  `cdp_pause.mjs` (N107).
- **Later overlays**: the runs reached turn 1-2 and at most one minigame; `heap=` after the fix was checked through
  w01, instDll and overlays 20/84, not every minigame. Other constant tags (if some REL uses a large one
  I missed in the grep for 7-8-digit hex nums in Malloc/FreeNum calls) would show the same way.
- **Real browsers/devices and Firefox/WebKit**: headless Chromium only.
