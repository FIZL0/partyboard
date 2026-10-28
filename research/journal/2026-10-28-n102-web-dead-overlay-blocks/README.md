# N102: a dead overlay's heap blocks on the web

Brief: `leftovers_free` (src/port/netplay_match.c, N57) frees the blocks of an overlay that is gone, the ones whose
allocating code `code_module` (dladdr) can no longer place. On the web it never ran: there is no dladdr, and
`HU_MEM_CALLER` (`__builtin_return_address`) is not a function pointer, so N99's `web_rel_function_owner` could not
place it either. A reload after a minigame left mid-play (N57: m427's screen copies, m440's objects, m415's anims)
kept those blocks.

## Outcome

Fixed. Each block's caller on the web is now an offset in the wasm module's code, and `web_rel.c` places that offset
in a REL (or the dol). The dead overlay's blocks are freed at the reload's scrub as on the desktop. In the browser,
with m415 killed mid-play by a reload (run m1), both tabs logged `gone 6` and `heap 2: freed 6 blocks of an overlay
that is gone`. Their heaps at the resumed instDll start then hashed `3B3C5032C767EFB5`, which is a fresh match start's
value. Both tabs' hash logs stayed identical in every field, heap included.

## What was wrong

- `code_module` is `#if defined(__linux__) || defined(__APPLE__)`. Emscripten defines neither, so it returned NULL
  for everything. The gate `code_module(netplay_match_tick) != NULL` was false, and the free loop never ran on the web.
- `__builtin_return_address(0)` is lowered to `emscripten_return_address(0)`. That call parses `new Error().stack` on
  every allocation and returns the frame's `wasm-function[N]:0x<offset>`, a byte offset in the module's code. It is
  not a table index, so nothing could map it to a REL.

## Fix

- `web_rel_caller_pc(level)` (EM_JS in `src/port/web/web_rel.c`) reads the same stack trace as
  `emscripten_return_address`. It counts only wasm frames, so the JS frames in between (the import itself and any
  wrappers) don't shift the level. It returns the code offset of frame `level`, where 0 is the function that called
  it. On Emscripten `HU_MEM_CALLER()` is now `web_rel_caller_pc(1)` (include/game/memory.h). This is not a regex:
  EM_JS's stringizing drops a regex literal's backslashes. The first build had `/\bwasm-function\[\d+\].../` and
  warned `unknown escape sequence`.
- N99's REL marks (`web_rel_mark.c`) now also store their own code offset (`web_rel_mark_pc =
  web_rel_caller_pc(0)`). `web_rel_pc_ranges` calls each mark once at boot. wasm-ld lays out the functions' code in
  index order, so a REL's code lies between its two marks' offsets. Boot checks: the offsets are nonzero and
  increasing, no two RELs overlap, and the order matches the function-index ranges. The boot log says `every REL's
  code offsets are between its marks'`.
- `web_rel_code_module(pc)` returns the REL, one fixed pointer for the dol, or NULL if the REL is not linked now (the
  same as dladdr after a dlclose), or if the offsets are unknown.
- `netplay_match.c`: `caller_module` uses `web_rel_code_module` on the web and `code_module` everywhere else.
  `dead_code_block` calls it. On the web the gate is `caller_module(HU_MEM_CALLER()) != NULL`. The desktop's gate is
  unchanged.
- `tools/web_pair.mjs`: `PAIR_ARGS_B`, tab b's command line instead of PAIR_ARGS (as N99 suggested), e.g. its own
  `--desync-at`.

Blocks are still tagged by caller, not by "the overlay that was current at allocation" (the brief's fallback). That
choice keeps the web the same as the desktop. If blocks were tagged by the current overlay, the dol's own allocations
made while an overlay runs (the board code under w01, HuSprAnimRead...) would count as the overlay's.

## Runs

All runs are in the browser (release, wasm-opt -O2), with `PAIR_SEED=0x18E2A0AF4FE4E301`, `--hash-log`, seats 3-4
CPU. Outputs are in build/n102/ (not in the repo). `harness/instrument.diff` was applied for the census runs. It adds
`n102_census`, which runs every 600 frames and at each overlay start: it reports every used block's caller as dol, a
linked REL by name, gone, or a sentinel. It also forces the round-end minigame to `N102_MG` (14 = m415) at instDll.

| run | build | result |
|-----|-------|--------|
| off1 | fix + census, offline `party.script` | Blocks are attributed: `m415Dll 49..56` while m415 runs, `m434Dll 12`, `m443Dll 6`, `w01Dll 2` on the board, everything else dol. `gone 0` at every overlay start of the normal flow |
| p0 | fix + census, pair | PASS, 24852 frames. m429 holds 8-10 of its own blocks mid-play. m414 holds none |
| d1, d2 | fix + census, pair, b `--desync-at 16500` | Not a test of this. Turn 2 went into a battle/Bowser minigame (no minigame point), and web_pair never pressed START at its instructions, so the flip landed in instDll, which is not hash-checked (N64). No resync. Filed N106 |
| **m1** | fix + census + m415 forced, b `--desync-at 8300` | Desync at 8310 inside m415, then a reload to the minigame point. Both tabs: `census start ... gone 6` (m415's six made-anim blocks, the count N57 saw on the desktop), `heap 2: freed 6 blocks of an overlay that is gone`, `board overlay 3 starts ... heaps 3B3C5032C767EFB5` (the fresh board start's value in every run). Hash logs: epoch 0 identical, heap included, until the planted flip at 8300; epoch 1 identical for all 15242 frames. The verdict is FAIL only because of the planted desync and because the resumed instDll stood (N106) |
| f1 | committed tree (no instrument), pair | PASS. `every REL's code offsets are between its marks'` on both tabs, turn 3, 2 minigames, hash logs identical in every field for 17737 frames |

Before the fix, the scrub never freed anything on the web, so m1's 6 blocks would have stayed (`gone 6` is measured
before `leftovers_free` runs). Two tabs that both reload keep the same leftovers, so their heap hashes would still
agree with each other. The split the brief asks about shows against a peer that did not play the minigame (a rejoin,
N57's j1), or as heaps that grow with every reload.

## Not verified

- **A pre-fix browser run of m1.** It needs a separate build of the parent with a different instrument, because the
  census uses the new `caller_module`. That build would show the heap hash at the resume start differing from
  `3B3C...`. I skipped it: by the code, the free loop could not run on Emscripten (see "What was wrong").
- **m427 and m440 killed mid-play in the browser.** Only m415 was forced. The mechanism is the same: their blocks
  come from the REL's own `HuMemDirectMalloc` calls.
- **A rejoin in the browser (fresh tab against a reloaded one).** No web harness does a rejoin. PAIR_DROP redials,
  which is not a reload.
- **The cost per allocation.** `web_rel_caller_pc` builds the same `new Error().stack` that
  `emscripten_return_address` built before, so it should cost about the same. Not measured. f1 ran fewer frames per
  second than p0, but both have the fix, so the gap is the machine's load from other slots, not this change.
- **wasm-opt inlining `HuMemDirectMalloc*` into a caller,** which would move the level-1 frame. It does not happen
  at -O2 in this build: off1/m1 attribute m415's blocks to m415, not to the dol. A different optimizer setting could
  change that. WEB_FAST builds (no wasm-opt) were not run.
- **Firefox/Safari stack formats.** Only Chromium was run. Firefox prints `wasm-function[N]:0x<off>` too. If a format
  doesn't match, the offsets come out as 0, the boot check prints `blocks' RELs unknown`, and the scrub frees
  nothing, as before this change.
