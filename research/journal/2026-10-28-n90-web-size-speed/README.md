# N90: web build size and speed

The N87 web build: partyboard.wasm 32.6 MB out of the link (-O1, plain Asyncify), 17.6 MB after `wasm-opt -O2`,
.data 10.1 MB, served uncompressed. In headless Chromium 154 (`tools/web_check.mjs`, the machine's GPU) the
scripted party ran at ~35 fps in real time: it could not hold 60.

An earlier session (wip/N90, ended before committing) left the ASYNCIFY_ONLY list tool, the compressed files and
the frame times in web_check.mjs. They are taken over here, checked, and fixed where the checks found holes.

## Result

| | main (N87) | N90 |
|---|---|---|
| partyboard.wasm out of the link (-O1) | 32.6 MB | 17.0 MB |
| partyboard.wasm after wasm-opt -O2 | 17.6 MB | 11.2 MB |
| same, brotli -q 11 / gzip -9 | (served raw) | 2.93 / 3.96 MB |
| partyboard.data | 10.1 MB | 8.1 MB (brotli 3.60, gzip 4.59) |
| download (wasm+data+js, as served by web_check) | 28.2 MB | 6.6 MB (brotli) |
| Asyncify-instrumented functions | 12567 | 2281 |
| party, real time (no --turbo), 80 s | 34.9 fps, frame p50 28.6 / p95 30.9 ms | 59.9 fps, p50 16.7 / p95 21.0 ms |
| party, --turbo, 80 s | 59.6 fps | 169.6 fps |
| main thread per frame (real time, CPU profile): wasm / JS / idle | 4.06 / 20.11 / 3.81 ms | 2.62 / 0.22 / 12.74 ms |

Each row's two builds ran at the same time, side by side, on this machine (load average 25-30 from other agents
throughout; absolute numbers move with it, the pairs are what to compare). Runs: `run8` (real time) and `run9`
(turbo), below.

## What changed

1. **Asyncify only where it can unwind** (`cmake/web.cmake` `PB_WEB_ASYNCIFY_ONLY`, default ON;
   `tools/web_asyncify_list.py`, from wip/N90). The probe link (no Asyncify, names, a link map) is analyzed: seeds are
   the async JS imports (emscripten_sleep, emscripten_fiber_swap, emwgpuWaitAny, fd_sync, ...); a function unwinds
   if it calls one that does; a `call_indirect` only reaches table functions of its own signature, and only the
   game's C (libdol's C objects, the RELs) has indirect calls that are taken as unwinding (process entries, object
   and event callbacks), plus SDL's main trampoline. Result as `-sASYNCIFY_ONLY=@list`. OFF gives back N87's
   ASYNCIFY_REMOVE build (used below as "main").
   - This replaces the brief's ASYNCIFY_IGNORE_INDIRECT + ASYNCIFY_ADD idea: ignoring all indirect calls would drop
     every game process entry, and the ADD list would have to be the same analysis by hand.
2. **REL functions have unique names** (`tools/web_rel_localize.py`): each REL's own functions become
   `<rel>.<name>` (symbol table and the object's name section; wasm-ld names functions from the latter). Before,
   the 91 RELs' `BoardCreate`, `fn_1_1548`, ... repeated and wasm-dis numbered the copies by function index, which a
   different link doesn't keep. The few names that still repeat (`ZombieWaitDevice` in SDL's audio and camera,
   statics repeated inside one REL) are listed as `name*`. Browser stacks now say which REL a function is in.
3. **The list is checked against the real link** (`web_asyncify_list.py --check`, a POST_BUILD step). It finds
   Asyncify's data global (what `asyncify_start_unwind` sets from its parameter) and takes the functions that read
   it as instrumented. The state global would be no test: with asserts every function reads it. Then:
   - every entry names a function, and every wildcard matches one;
   - every exact entry is instrumented;
   - no function calls an async import or an instrumented function directly without being instrumented itself;
   - partyboard.js has no async import the tool doesn't know.
4. **The probe links with partyboard's debug flags** (`pb_web_link`). With `-g2` emcc links another build of
   emdawnwebgpu, where `(anonymous namespace)::EventManager::WaitAny` is a function of its own (inlined in the -O2
   one). The WEB_DEBUG build trapped at the first WebGPU wait (`unreachable` at boot) until then. The new `--check`
   finds exactly this: `not instrumented: (anonymous namespace)::EventManager::WaitAny(...), which calls
   emwgpuWaitAny`.
5. **aurora: the frame's GPU data without mapping in the browser**
   (`cmake/patches/aurora/0007-web-staging-writebuffer.patch`, `__EMSCRIPTEN__` only). The CPU profile showed 72%
   of the main thread (28 of 38 ms a frame in turbo) in one JS function, emdawnwebgpu's unmap callback. aurora maps
   the whole staging buffer every frame (24 MB uniforms + 3 vertex + 1 index + 8 storage + 24 texture upload =
   61 MB). In the browser a mapped range is a heap copy: `GetMappedRange` memaligns one and `Unmap` copies all of it
   into the GPU buffer, however little the frame wrote. Now a frame writes into one heap buffer (no threads: a frame
   ends before the next begins). end_frame `queue.WriteBuffer`s each region's used bytes (`align4(size())`; every
   copy out of staging stays within that) into a staging buffer made `CopyDst|CopySrc`, at the point where it
   unmapped before, so before the submit of the frame's commands. "Mapping" a slot is immediate. Heap: one 61 MB buffer,
   instead of up to four mapped 61 MB copies.
6. **Precompressed files** (`tools/web.sh`, from wip/N90): a release build writes `.br` (brotli -q 11) and `.gz`
   next to partyboard.wasm/.data/.js for servers that serve them (nginx gzip_static/brotli_static, Caddy
   `precompressed`). `web_check.mjs` serves them by Accept-Encoding like such a server (`WEB_NO_COMPRESS=1`: raw).
   Chromium took `br` on http://127.0.0.1 (logged: `served compressed: ...`).
7. **The prelaunch background is not in .data** (from wip/N90): on the web the prelaunch UI shows only without a
   usable disc (`portmain.cpp`: `skipPreLaunch = !forcePreLaunchUI`), and there it is a dead end anyway (no file
   dialog). -2 MB.
8. **`tools/web_check.mjs`**: `FRAME_STATS` (frame times from the canvas's `getCurrentTexture`, one per drawn
   frame, per second and over a window into OUT/frames.txt), `PROFILE=a-b` (Chromium's CPU profile of those seconds:
   OUT/profile.cpuprofile for DevTools, OUT/profile.txt with the main thread's ms in wasm/JS/GC/idle per frame and the
   top wasm functions), `WEB_ROOT` (serve another build dir), `DUMP_FILES` takes a directory.

## Checks

All runs: headless Chromium 154, `tools/web_check.mjs` with the default GPU flags, the scripted party
(`tools/netplay/party.script --match 0@1500`), disc GMPE01 from `PB_DISC`.

- **Same game, every field.** Per-frame `--hash-log`, plain Asyncify vs ASYNCIFY_ONLY (before the aurora patch): all
  8354 frames equal, `heap=` included (`run1`, 200 s turbo).
- **No frame left out of an unwind, at runtime.** WEB_DEBUG build (Asyncify's asserts: an instrumented function
  traps when a call it made unwound through a function that wasn't instrumented) of ASYNCIFY_ONLY, 360 s turbo:
  16661 frames, title → file select → board w01 (two turns, all four players), minigames (overlays 23 and 42),
  results (84), no trap, no page error. Its hashes equal the release runs' for the 8354 frames they share.
- **The aurora patch draws the same pictures.** Hashes, ASYNCIFY_ONLY with and without 0007: every field but
  `heap=` equal for 7094 frames (`run4`). `heap=` differs from frame 1 because it hashes a block's `num` as a raw
  address when it is outside the HuMem heaps, and the one 61 MB allocation at init moves malloc's layout (gameplay
  fields are what lockstep compares). Pictures: `PB_ICON_SHOT=/tmp` (N60) via `?env=`, copied out with
  `DUMP_FILES=/tmp`, both builds stopped at the same game frame (`STOP_ON='reseed at frame 6182'`, before the first
  minigame so no overlay is shot twice). 5 of 7 shots are pixel-identical. The other two differ in the face of the
  spinning dice block (ON: 3, patched: 1). Two runs of the same patched build differ there too (4627 px), and one of
  them equals the unpatched build's shot exactly. So it's timing (the face follows something real-time), not the
  patch. Earlier, an unpatched run showed the file select's rays background where another unpatched run and both
  patched runs showed it dark: also run to run.
- **Desktop unchanged:** `tools/check.sh` passes. 0007 is all `#ifdef __EMSCRIPTEN__`. The patch series 0001-0007
  applies in order to a clean `git archive` of aurora's commit and gives the tree that was built.
- **Sizes and the instrumented counts** come from the builds' own output (`web_asyncify_list: 2281 of 22792
  functions instrumented; by signature alone 12557, plain Asyncify 12567` before 0007; 2281 of 22789, 12554, 12564
  with it; the shipped wasm is byte-identical to the one measured as "N90").

## Audit of the language rule

The rule is what's unsound in principle: C++ and the C libraries' indirect calls are taken as never unwinding.
Where it decides (the probe's non-game functions that call through a pointer of a signature some unwinding
function has):
- Game C function pointers that C++ calls are aurora's SDK callbacks. Of the `( -> )` ones only
  GXDrawDone/GXSetDrawDone's draw-done callback appears, and the game never sets one. VI/DVD/card callbacks run in
  interrupt context on the GameCube and never wait.
- The port's C++ (online.cpp, settings, RmlUi components) calls std::function/virtual callbacks. None of them waits
  for a frame: they run inside the UI's update, not in a game process.
- One real gap: sqlite's VFS calls `unixSync` through a pointer, `fsync` → `fd_sync` always unwinds under Asyncify
  (it returns a Promise), and sqlite's frames aren't instrumented. Nothing opens a database in the browser build
  (pipeline_cache returns early under `__EMSCRIPTEN__`; gpu_cache's callbacks are Dawn-native only). If aurora ever
  keeps a cache in the browser, this has to change. The tool's docstring says so.

## Tried and dropped

- **Wrapping every wasm export** in the page to measure time "in wasm" per frame: the wrapper cost so much
  (26 fps instead of 41) that it measured itself, and it counted the JS imports (all of WebGPU) as wasm. The
  sampling CPU profiler (`PROFILE`) replaced it.
- **Wall-clock frame times alone**: plain vs ONLY gave the same 41.1 fps / 24.3 ms (`run1`), because the frame was
  bound by the staging copies, not by wasm. The profile showed wasm 6.30 → 5.21 ms a frame (-17%) for Asyncify
  ONLY at equal JS time (`run3`).
- **JSPI instead of Asyncify**: Emscripten 6.0.13's fibers exist only under Asyncify (`emscripten_fiber_swap`
  drives `Asyncify.state`; there's no JSPI variant), and libco needs fibers. A JSPI coroutine shim of our own (each
  cothread its own `promising` entry, a switch resolves the target's promise and suspends on its own, with the C
  stack pointer swapped by hand) was not attempted. It would also need JSPI in Firefox and Safari. With ONLY the
  remaining Asyncify cost is ~1 ms a frame.

## Found on the way

- emdawnwebgpu's `_emwgpuBufferGetMappedRange` clears the copy with `HEAPU8.fill(0, data, mapped.byteLength)`. The
  third argument is an end, so it clears nothing whenever the copy sits above `byteLength` in the heap: a new
  mapping for write starts with stale heap bytes, not zeros as WebGPU says. aurora no longer maps for write in the
  browser, so it doesn't matter here. Not reported upstream (no publishing from this run).
- The release `partyboard.wasm` has no function names: `wasm-opt -O2` in tools/web.sh drops the name section that
  `--profiling-funcs` put in. Profiles of the release build show `wasm-function[N]`. Not changed: names cost size;
  a WEB_FAST or WEB_DEBUG build keeps them.
- The scratchpad filled the disk quota once: `wasm-dis` output of the module is ~1 GB a copy. Delete .wat dumps.

## Not verified

- **Real devices and other browsers**: only headless Chromium 154 on Linux (AMD) was run, no Firefox/WebKit run of
  the N90 build. The aurora change uses only `queue.writeBuffer`, plain WebGPU, but Firefox's path was not run.
- **Every Asyncify path**: the debug run covered boot, title, file select, board w01, two minigames and results.
  Story mode (mstory*, 300+ listed functions), other boards, the menus' Settings/Online pages and the prelaunch
  screen did not run under the asserts. The static check covers the direct calls in all of them. Indirect calls
  rest on the rule above.
- **Online play in the browser** (N92's two-tab WebRTC pair) was not run with this build. Lockstep compares
  gameplay fields, which were equal; `heap=` moves (above).
- **Memory on a phone**: the heap is 61 MB smaller at peak (one staging copy instead of up to four), not measured
  on a device.
- **Serving `.br` from a real server** (nginx/Caddy config): only web_check's own server did it.
