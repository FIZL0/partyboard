# N99: browser-to-browser desync (seed 18E2A0AF4FE4E301)

Brief: two headless Chromium tabs (tools/web_pair.mjs) desynced once at frame 4050 in N92's run 2; N98's run 12 hash
logs showed the heaps' layout splitting first (frame 467, board w01's intro) and the state at 2615. Find the first
differing field and the cause, and check whether the desktop pair has it too.

## Cause

**bootDll's `BootExec` (the title screen's root process) kept running under the board in the browser.**
`MatchStartProc` (src/port/netplay_match.c) kills the menu overlay's own processes with
`HuPrcKillIf(overlay_process, ...)`, and `overlay_process` asks `code_module()` which shared library holds a
process's func/dtor (dladdr). The web build links every REL statically into one wasm and has no dladdr, so
`code_module` returned NULL for everything: **every web match start logged `killed 0 menu processes`** (all runs
below before the fix; the desktop logs `killed 1`). The match starts from the title (web_pair.mjs and the real menu
both host/join over bootDll), so `BootExec` went on with its title/demo loop (wipes, sprite groups, the demo
window's `HuWinKill`, 180-frame waits, `OSGetTick` waits in real time) in the board, from wherever each tab's title
loop was when the match started. Two tabs sit at the title for different times, so their `BootExec`s were at
different points: they allocated and freed at different frames (the heap layout split), and sometimes what it did
reached the board's state (its wipes and window kills land in the board's wipe/window state; the board waits on
both). The same leftover survived every resync reload too (`killed 0` there as well), which is why reloads in N92/N98
desynced again. The `OSGetTick` waits may explain N92's note that the slow (loaded) runs desynced earliest.

First differing allocation (run a1, the N74-style alloc log on the web, `harness/instrument.diff` +
`harness/n99_mem.c.inc`; stacks from the browser's JS stack, so REL functions show as `<rel>.<name>`):

```
a: f1285 ovl89 A h0 off 78d60 size 40 num ffffff00: HuMemDirectMalloc HuSprGrpCreate bootDll.BootExec co_entry
a: f1333 ovl89 F h0 off 78640 size 320 ...: HuMemDirectFree HuWinKill bootDll.BootExec co_entry
b: f1370 ovl89 A h0 off 78d60 size 40 num ffffff00: HuMemDirectMalloc HuSprGrpCreate bootDll.BootExec co_entry
```

The board (ovl 89 = w01) is running and `bootDll.BootExec` still allocates, 85 frames apart on the two tabs.

## Fix

- `src/port/web/web_rel_mark.c` (new): one function, compiled twice per REL by cmake/web.cmake as
  `pbrel_first_<rel>` / `pbrel_last_<rel>`, each returning its own constant (so wasm-opt's duplicate function
  elimination keeps them apart). The prelink is now `emcc -r first.o <REL objects> last.o`: wasm-ld keeps an
  object's functions together and in input order, so each REL's code lies between its two marks in the module's
  function index space.
- `tools/web_rel_localize.py` keeps the marks global (`KEEP [MARK...]`), like ObjectSetup.
- `src/port/web/web_rel.c`: `web_rel_function_owner(fn)` (declared in include/port/web_rel.h). A function pointer
  is a table index; the table entry is a JS function whose `name` is its function index (WebAssembly JS API), read
  by a small `EM_JS` (`getWasmTableEntry`). At boot every REL's range is checked (first < ObjectSetup < last, no
  overlaps): the log says `web_rel: every REL's code is between its marks`, or names the REL that broke it and then
  knows nothing (the old behaviour) instead of guessing.
- `src/port/netplay_match.c`: `function_module()` = `web_rel_function_owner` on Emscripten, `code_module`
  elsewhere; `overlay_process` and the two kills (match start, match leave) use it. `code_module` itself is unchanged:
  `dead_code_block` gives it allocation return addresses, which are not table indices on wasm (N102).
- Desktop/Android/iOS: unchanged (`function_module` is `code_module` there).

## Also in this task

- **main's web build was broken**: N86's commit a86810a5 (iOS IPA) dropped N87's lines from `CMakeLists.txt`
  (`PB_DOL/REL_LIBRARY_TYPE`, `include(cmake/web_early.cmake)`, `include(cmake/web.cmake)`); `tools/web.sh` failed
  in fmt's `os.cc` (`malloc` undeclared, web_early's `-includecstdlib` gone). Put back. Slot s2's N91 (not merged
  yet) restores the same lines with its own `include(cmake/pbmath.cmake)` next to `include(cmake/web.cmake)`; to
  keep both merges clean the first three hunks here are identical to N91's, this branch includes web.cmake after the
  ANDROID block instead (a few lines further down), and web.cmake/web_early.cmake have `include_guard(GLOBAL)`, so
  with both merged the second include is a no-op (checked: configure with both include lines passes).
- `tools/web_pair.mjs`: `WEB_ROOT` (serve a copied build while other runs use build/web, as web_check.mjs has).
  Run rl1 also used a local `PAIR_ARGS_B` (tab b's command line instead of PAIR_ARGS, for `--desync-at` on one peer:
  `const line = (name === 'b' ? process.env.PAIR_ARGS_B : undefined) ?? process.env.PAIR_ARGS;` before the
  `args` line in `tab()`), not committed: N98 (on main, not on this branch) rewrote those lines, and the two would
  conflict. Worth adding on top of N98's version.
- Filed **N103** (m418 traps on the web: its sequence tables call `void fn(s32)` through `void(void)` pointers, wasm
  checks call_indirect types) and **N102** (leftovers_free never runs on the web).

## Runs

All with `PAIR_SEED=0x18E2A0AF4FE4E301 PAIR_ARGS='--hash-log /tmp/h.log' DUMP_FILES=/tmp/h.log`, seats 3-4 CPU,
both humans tapping A (fake pad, wall clock). Hash logs split at each `^0 ` line (one file per epoch: menu, then
epoch 0, 1, ...), compared with `tools/netplay/hashdiff.py`. Outputs in build/n99/ (not in the repo).

| run | build | result |
|-----|-------|--------|
| r1 | before the fix, WEB_FAST | **reproduced**: desync at frame 3780, reload; desync again at 2580 of epoch 1, reload. hashdiff epoch 0: heap layout from **1296**, state from **3721** (sys, frand, rand8, brand); epoch 1: heap and sys both at 2043; epoch 2 differs at frame 0 (sys). `killed 0 menu processes` at every start |
| a1 | before the fix + alloc log | no desync (to frame 7450, where N97's START at the instructions ended the match), but heap layout split at **1285**; alloc logs differ from line 5081: `bootDll.BootExec` above |
| f1, f2 | fix + alloc log, WEB_FAST | **PASS**, 14436 / 14500 frames, `killed 1 menu processes` on both tabs; hash logs identical in every field, heap included (no heap note), for 14536 / 14532 frames (f2's last line was cut by the dump) |
| g1, g2 | fix, release (wasm-opt -O2) | **PASS**, 18202 frames each; hash logs identical, heap included, for 18248 / 18249 frames: turn 1, minigame (ovl 22), its results (84), all of turn 2, into turn 2's instDll |
| rl1 | fix, release, `PAIR_ARGS_B` with `--desync-at 3000` | the flip on b desynced at 3000, reload to the turn-1 checkpoint (`killed 0 menu processes`, `ran 2 board process destructors`), then 4148 frames in step (hash every 30 frames, no desync) until minigame m418 trapped in both tabs (`function signature mismatch` in `m418Dll.fn_1_9BA4`, N103; not this fix: m418's tables). No hash logs (the harness stopped at the trap) |
| h1 | fix, the committed tree, WEB_FAST | **PASS**, 14554 frames, `killed 1 menu processes` and `every REL's code is between its marks` on both tabs; hash logs identical, heap included, for 14590 frames (turn 1, minigame 22, results, turn 2, into instDll) |

Before the fix the old N92/N98 numbers fit: the frame of the first split follows how long each tab sat at the title
(run 12: 467, r1: 1296, a1: 1285), and whether the state follows depends on what BootExec does then.

### Desktop

- **d2** (`harness/desk_pair.sh`: N89's two-copy harness through the real Online menu, host and guest, Start Match
  from the title; the host sat 2400 frames at the title, the guest 1598): both log `killed 1 menu processes`; hash
  logs identical in every field, heap included, for all 7724 match frames. **The desktop pair doesn't have this
  desync**: dladdr places BootExec in bootDll's .so and the match start kills it.
- d1 failed as a run: the host wrote its code at frame 360 before its lobby had one (empty code); steps moved later.
- Tried first and dropped: one desktop process with `--script harness/title.script --match 0@700` vs `0@1000` (two
  title times in one process). Not comparable: a scripted match reseeds from the session seed and its start frame
  (`netplay_reseed`, N26), so the two runs differ at frame 4 by design. `harness/from.py` (renumber a log from a
  frame) is left from that.

## Harness

- `harness/instrument.diff` + `harness/n99_mem.c.inc` (append to src/port/netplay_match.c): every HuMem alloc/free
  during a match to `/tmp/alloc.log` with frame, heap, offset, size, num and the JS stack's function names
  (`emscripten_get_callstack`); `DUMP_FILES='/tmp/h.log /tmp/alloc.log'` copies it out. Strip `partyboard.wasm.`
  before diffing.
- Naming frames of a release (no names) wasm trap: rerun the same `wasm-opt -O2` with `-g` on
  build/web-cmake/partyboard.wasm, read the name section (the code offsets of the trap's stack matched).
- `harness/desk_pair.sh`, `desk_host.txt`, `desk_guest.txt`: N89's run.sh with `--hash-log`, no quit; apply N89's
  `ui-hook.diff` by hand (it has no hunk headers) and revert it after.

## Not verified

- **Which BootExec call reached the board state** in r1 (sys at 3721): r1 had no alloc log, and the run with one
  (a1) only split in the heap. Wipes and `HuWinKill(demoWinId)` (an id the board may own by then) are the likely
  ones; not traced, since with BootExec gone neither happens.
- **Real browsers, real devices, two machines**: headless Chromium on this machine only (N95).
- **The marks under other toolchains**: checked with this emsdk (6.0.13) in WEB_FAST and release (wasm-opt -O2);
  another wasm-ld or wasm-opt that reorders functions would make the boot check say so and kill nothing (the old
  behaviour), not misplace processes.
- **A reload inside a minigame on the web** with the fix: rl1's reload was on the board; the minigame it then
  reached traps for its own reason (N103). Also N102 (leftovers_free off on the web).
- **Browser vs desktop in one match**: not tried (N91/N96).
- **The web build of the merge with main** (N98's web_ui_probe.cpp, exports and web_pair.mjs on top of this):
  `git merge-tree` with main is clean, but the merged tree was only built for the desktop by the runner's gate, not
  for the web here. With N91 (slot s2, not merged yet) both restorations of the CMakeLists.txt web lines meet; the
  include guards make that a no-op (configure checked with both include lines), the merge itself not tried.
