# N87: Party Board in the browser (Emscripten, wasm32, WebGPU)

`tools/web.sh` builds `build/web/`: the page (`platforms/web/index.html`, `pb.js`), `partyboard.js`,
`partyboard.wasm` and `partyboard.data` (only `res/`: fonts, RmlUi documents). No game data is in the build: the
player picks their disc image in the page, which keeps it in the origin's private storage (OPFS) for the next
visit and hands it to the game as an in-memory file (`PB_DISC=/disc/<name>`). This replaces S1a's feasibility
spike (S1a is not ticked by this task; it can be closed as done by N87).

## How it's built

- **Toolchain:** emsdk 6.0.13 (`~/emsdk`, `./emsdk install latest`), rustup's `wasm32-unknown-emscripten` (for
  aurora's nod), configure preset `web` (`CMakePresets.json`, `build/web-cmake`), build preset `web`.
- **Graphics: aurora on the browser's WebGPU through emdawnwebgpu** (Dawn's own `webgpu.h`/`webgpu_cpp.h` over
  `navigator.gpu`, an emcc port: `--use-port=emdawnwebgpu`). aurora had no Emscripten support left (its 2022
  "initial emscripten support" commit was undone since). `cmake/patches/aurora/0004-emscripten.patch`:
  - `AuroraDawnProvider.cmake`: on Emscripten `dawn::webgpu_dawn` is an interface target with the port's flag, plus
    a `<dawn/webgpu_cpp.h>` shim (the port only has `<webgpu/webgpu_cpp.h>`).
  - `gpu.cpp`: no `dawn::native` instance descriptor, no Dawn toggles/cache descriptor, no device logging callback
    (not in emdawnwebgpu), adapter request with `BackendType::Undefined` (the browser picks).
  - `BackendBinding.cpp`: the surface is `EmscriptenSurfaceSourceCanvasHTMLSelector` on SDL's canvas
    (`SDL_PROP_WINDOW_EMSCRIPTEN_CANVAS_ID_STRING`, already `#canvas`).
  - No threads (no `-pthread`, so no COOP/COEP and no SharedArrayBuffer): the render worker never starts, every
    item runs where it's enqueued (its existing not-running path) and `is_worker_thread()` is true; no pipeline
    compile thread and no pipeline cache (sqlite on a memory FS would be lost anyway).
  - `aurora.cpp`: `BACKEND_WEBGPU` is the backend; `aurora_update()` calls `emscripten_sleep(0)`, the one place
    every loop iteration passes (game loop, prelaunch loop, lockstep WAIT), so the browser gets control (events,
    presenting the canvas) once per frame.
  - Two `uint64_t` -> `size_t` narrowings that wasm32 rejects (`dds_io.cpp`, `texture_replacement.cpp`).
  - imgui 1.91.9b's `imgui_impl_wgpu.cpp` refuses Dawn mode under `__EMSCRIPTEN__` (its Emscripten path is the old
    `-sUSE_WEBGPU`, gone from Emscripten); a copy with that macro renamed is built instead.
  - Plus the run-time fixes below (frame waits that yield, no `Present`, CPU adapter, nod without its preloader
    thread, RmlUi's stencil pipelines).
- **SDL3:** aurora's vendor build (3.4.10 from source), SDL's own Emscripten backend.
- **libco: Emscripten fibers** (`src/port/web/libco_emscripten.c`, Asyncify), linked instead of
  `extern/libco/libco.c`. Each cothread has its C stack (process.c's size, >= 64 KB) and a 256 KB Asyncify buffer.
- **RELs linked statically, no dlopen** (`cmake/web.cmake`, `tools/web_rel_localize.py`, `src/port/web/web_rel.c`):
  - On Emscripten a `SHARED` library is a relocatable object that would carry its own copy of everything it links
    (dol, musyx), so dol is `STATIC` and each REL an `OBJECT` library (`PB_DOL_LIBRARY_TYPE`/`PB_REL_LIBRARY_TYPE`
    in `CMakeLists.txt`, set by `cmake/web_early.cmake`).
  - Each REL is compiled with `-DObjectSetup=ObjectSetup_<rel>`, prelinked (`emcc -r`), and its symbol table
    rewritten: every symbol it defines becomes local except `ObjectSetup_<rel>` (the RELs share 1170 global names,
    `lbl_1_data_0` alone in 62 of them), and its `.data.*`/`.bss.*` segments become one segment `pbrel_<rel>`, so
    wasm-ld gives it `__start_pbrel_<rel>`/`__stop_pbrel_<rel>`. (`#pragma clang section` would have been the
    compiler way to name the segment, but clang emits an empty section name for it on wasm.)
  - `web_rel_table.c` (generated) lists name, ObjectSetup and data range per REL. At boot (a constructor) each REL's
    data is copied aside; `omDLLLink` -> `web_rel_link` copies it back on the first link since the last unlink, which
    is what dlopen after dlclose gives the desktop (fresh .data/.bss). 542 KB of REL data in all, 91 RELs
    (`?env=PB_WEB_REL_LOG` prints each range).
  - `objdll.c` / `object.h`: an `__EMSCRIPTEN__` branch next to the dlopen one.
- **Asyncify and wasm exceptions.** Rust's std for `wasm32-unknown-emscripten` is built with wasm exception
  handling (nod's `__cpp_exception` references), so the link has `-fwasm-exceptions`, and then setjmp/longjmp has to
  be wasm's too (`-sSUPPORT_LONGJMP=wasm` everywhere: Emscripten refuses JS sjlj with wasm EH). Binaryen's Asyncify
  pass emits invalid code for a function with exception handling in it ("non-final block elements returning a
  value must be dropped"). No such function ever is on the stack when the game yields or switches fibers (Rust,
  libc++abi, libpng/freetype error paths, nlohmann's errors), so:
  - `partyboard_eh_probe` (same objects, `-sASYNCIFY=0 --profiling-funcs`) is linked first; `tools/web_eh_list.py`
    reads its `wasm-dis` and lists every function with `try`/`try_table`/`throw`/... (756 here); that list is
    `ASYNCIFY_REMOVE`. It is too long for one argument (wasm-opt gets the whole list as one: E2BIG), so emcc's
    response file `web_asyncify_remove.rsp` holds one entry, `@<build>/web_asyncify_remove`, which reaches binaryen
    as `asyncify-removelist@@<file>` and binaryen reads the file itself (emcc turns `.` `#` `?` in that entry into
    ` ` `&` `,`, so the build dir's path can't have them; checked at configure). Names go in verbatim: with ~700
    wildcard patterns the Asyncify pass took minutes (binaryen globs every pattern against every function); only
    the few names wasm-dis numbered (`_123`, repeats) or with unpaired brackets are patterns.
  - The partyboard link stops at `-O1`: wasm-opt's -O2+ inline before Asyncify runs, and inlining a removed
    function into an instrumented one would put exception handling back into it. `tools/web.sh` runs
    `wasm-opt -O2` on the result.
  - The C/C++ is compiled without exception handling (a throw from libc++ or nlohmann then goes up as a wasm
    exception through frames that can't catch it, and aborts). The port catches C++ exceptions in `config.cpp` (a missing config.json is a `std::system_error`), `achievements.cpp`
    and `prelaunch.cpp`: those three are compiled with `-fwasm-exceptions` (and so land in the skip list).
- **Two prototypes the desktop got away with:** wasm traps on a call whose signature isn't the definition's, and
  wasm-ld turned `BoardSpaceBlockExec`'s call into an invalid module. `space.c` declares `BoardBlockExec`,
  `BoardMushroomExec`, `BoardBooHouseExec` with `(player, space)` but they take `(player)`; `bowser.h` declares
  `BoardBowserExec` void, it returns s32. Fixed under `#ifdef __EMSCRIPTEN__` only (the decomp's `// wrong`
  declarations stay for the desktop, where the extra argument is ignored).
- **pbnet:** the C stub (`PBNET_STUB=ON` in the preset): no online play in the browser yet (below).
- `fmt` 11.1's `format.h` calls malloc/free without `<cstdlib>`; Emscripten's libc++ doesn't pull it in
  (`-includecstdlib` for C++, `cmake/web_early.cmake`).
- Netplay's OSGetTime/PADRead hooks use the `define` route (`PB_NETPLAY_HOOK=define`).
- `--profiling-funcs` in every link: function names in the wasm (+~2 MB), so the console's and the debugger's
  stacks are readable (they found most of what follows).

## What it took at run time (each found in headless Chromium, in this order)

- **The browser needs control back every frame.** `aurora_update()` yields (`emscripten_sleep(0)`), the frame
  limiter sleeps with `emscripten_sleep(ms)` instead of blocking (`src/port/imgui.cpp`).
- **A CPU adapter is the only one Chromium headless offers by default** (SwiftShader): aurora refused it
  (`allowCpu`); on the web the browser's adapter is taken whatever it is.
- **nod's preloader thread:** `std::thread::spawn` panics without pthreads; aurora opens the disc with
  `preloader_threads = 0` (nod's single-threaded reader) on the web.
- **`aurora::gfx::begin_frame` spun for ever** in `acquire_frame_slot`/`wait_for_staging_buffer` calling
  `wgpuInstanceProcessEvents`: the browser settles a buffer map only once JS gets control back. Those waits now
  yield too (`process_gpu_events`, `lib/gfx/common.cpp`).
- **`wgpuSurfacePresent` is unsupported in the browser** (emdawnwebgpu aborts): the canvas presents when the page
  yields, so `end_frame` skips it.
- **A fiber's C stack must be 16-byte aligned.** The game hung in `HuMemHeapDump`'s second `OSReport`, inside
  musl's `__stdio_write` writing an empty iovec for ever (`fd_write(iov, iovcnt=1)` with `iov` still on the empty
  first entry). clang computes `&iovs[1]` of a stack array as `(sp + 16) | 8`, which assumes a 16-aligned stack;
  the fiber stacks were `malloc`ed (8-aligned), so `| 8` added nothing. `aligned_alloc(16, size & ~15)` fixed it.
  (Found by pausing the page from CDP every second, `PAUSE_AT`, and reading `fd_write`'s arguments in the paused
  frame, `PAUSE_EVAL`; the same printf in a fiber in a small test was fine because its stack happened to be
  aligned.) Anything else that hands wasm code a stack must align it the same way.
- **Chromium headless's SwiftShader canvas loses the device on its second texture** ("Could not find
  SharedImageBackingFactory ... WebgpuSwapChainTexture, format: RGBA_8888", then `device.lost` "destroyed"). A
  plain WebGPU page clearing a canvas does the same with the preferred `rgba8unorm` (fine with `bgra8unorm` there,
  not in the game), so it's the environment: `tools/web_check.mjs` runs Chromium on the machine's GPU
  (`--use-angle=vulkan --enable-features=Vulkan --use-vulkan=native --ozone-platform=headless`, AMD RDNA4 here),
  where the preferred format works.
- **RmlUi's stencil-only pipelines** left `depthWriteEnabled` Undefined, which emdawnwebgpu passes on as true; the
  browser validates it (native Dawn runs with `skip_validation`): set to False on the web.

## The page

- `navigator.gpu` missing: a message, no game (there is no WebGL2 path, below).
- Disc: `<input type=file>`; the file is streamed into OPFS (`disc/<name>`, `createWritable`), read whole into memory
  (`File.arrayBuffer()`) and given to the FS with `createDataFile(..., canOwn)` (no second copy). Next visit: Play
  with the stored disc, or Forget. Without `createWritable` (Safari before 26) the disc is used for that visit only.
- User dir: SDL's pref path `/libsdl/MarioPartyRD/Party Board/` on IDBFS, loaded before main, saved every 5 s and on
  `pagehide`.
- `?args=` passes the game's command line (`--script`, `--match`, `--turbo`), `?fetch=a,b` puts the server's
  `test/a` in the game's `/test` (for `tools/web_check.mjs`), `?autoplay=1` starts with the stored disc.
- The prelaunch UI is skipped on the web when the disc is usable (it needs a file dialog and a hashing thread).
- No touch.js (opentennis has one): the game draws its own touch pad (N70, RmlUi) from SDL finger events, which
  SDL's Emscripten backend makes from the browser's touch events. Not tried on a touch screen.
- WebGPU only, no WebGPU/WebGL2 pick as opentennis does: aurora has no GL renderer for the browser.

## Checks

`tools/web_check.mjs DISC OUT [SECS] [QUERY]`: serves `build/web`, starts headless Chromium (`--headless=new
--enable-unsafe-webgpu` and the GPU flags above), picks DISC with `DOM.setFileInputFiles`, screenshots, console in `OUT/console.txt`.

Env: `CHROME_FLAGS` (instead of the GPU flags), `SHOT_EVERY`, `WEB_KEYS` ("22:Enter,30:F1"), `PAUSE_AT` +
`PAUSE_EVAL` (pause the page from CDP and log the stack / evaluate in the top frame: how the stalls above were
found), `WEB_PRELOAD` (JS before the page, e.g. wrap a WebGPU call and `console.trace` it), `WEB_PATH`, `WEB_FILES`
(served at `/test/`), `DUMP_FILES` (copy files out of the game's FS, e.g. a hash log). Chromium's own log goes to
`OUT/chrome.log`. Disc: `$PB_DISC` (GMPE01 Rev 1, `.ciso`, 600 MB). Chromium 154, AMD RDNA4 over Vulkan.

- **Boots to the title screen** (`01-title-chromium-headless.jpg`): the page picks the disc, the game opens it,
  bootDll, the logo and the title by 20 s; no WebGPU validation error, no page error, 90 s.
  `node tools/web_check.mjs $PB_DISC OUT 90`.
- **Offline party starts and plays** (`02-party-board-w01.jpg`, `03-party-minigame.jpg`): the desktop's scripted
  party, `?args=--script /test/party.script --match 0@1500 --turbo&fetch=party.script` with
  `WEB_FILES=tools/netplay/party.script`, 200 s: board w01 (overlay 89) from frame 1501, every player's turn 1, the
  minigame start (instDll 3, m415Dll 23, result 84) and back to the board into turn 2, frame ~15500, at ~72 frames/s
  in turbo. The REL relinking (fresh data per link) went through w01 -> instDll -> m415 -> resultDll -> w01.
- **Same game as the desktop build**, mostly: the Linux build (`build/linux-default-relwithdebinfo`, GCC) on the
  same script reaches the same reseed frames (4151, 5217, 6182, 7071, 8494, 11201, 11513, ...). Per-frame hash logs
  (`--hash-log`, copied out with `DUMP_FILES`): every field is equal for frames 0..10868 (except `heap=`, the
  heap layout, which differs from frame 0: 32-bit pointers make other block sizes), then `plr3` differs at 10869
  inside m415Dll, `plr0`/`plr1` at 11505, `plr2` at 13837, while `sys`, `cfg`, `pad`, `frand`, `rand8`, `brand`
  stay equal through 15493. Two Linux runs agree for 52586 frames, so it's the platform. Not dug into (N91): likely
  libm (`ext_math.h`'s `sind`/`cosd`/`atan2d` call double `sin`/`cos`/`atan2`; musl's and glibc's can differ in
  the last bit). Until then a browser peer can't lockstep with a desktop one.
- **Second visit:** same Chromium profile, no file picked, `?autoplay=1`: the disc comes from OPFS, overlay 1 at
  0.8 s (no download or pick).
- **Keyboard reaches the UI:** F1 opens the menu bar (Settings / Online / Achievements / Quit). The game shows "No
  controller assigned" as the desktop does without a pad; Enter at the title does nothing until port 1 is bound in
  Settings -> Input (not done here).
- **Release build:** `tools/web.sh` (with `wasm-opt -O2`): partyboard.wasm 17.6 MB (6.5 MB gzip -9), .js 0.4 MB,
  .data 10.1 MB (6.6 MB gzip, the fonts), boots to the title the same (the second-visit and keyboard runs used
  it; the party runs used `WEB_FAST=1`). ~25 s incremental, the first build fetches
  and builds every dependency (SDL3, nod with cargo, abseil, freetype, ...).
- **Desktop unchanged:** `tools/check.sh` passes; the Linux build runs the same party (above). Every C/C++ change
  outside `cmake/web*.cmake` and `src/port/web/` is under `__EMSCRIPTEN__` / `EMSCRIPTEN`, except aurora's two
  `size_t` casts (no-ops on 64-bit) and `process_gpu_events` (the same `ProcessEvents` call on the desktop).

## Tried and dropped

- Hiding nod's Rust std like `cmake/pbnet.cmake` does on the desktop (prelink `libnod.a` with `--whole-archive`,
  make every symbol but `nod_*` local, so Rust's compiler_builtins `fmodf`/`memcpy`/`__multf3` can't stand in for
  libc's): the link then produced an invalid module (`adler32_stub`: nod bundles zlib-ng, which became local next to
  the C build's global zlib-ng). Localizing only data symbols was valid, only functions gave duplicate symbols.
  Dropped: nod is linked as is; its builtins are leaves and in the Asyncify skip list.
- `-sSUPPORT_LONGJMP=emscripten` with `-fwasm-exceptions`: refused by emcc.
- JSPI instead of Asyncify (no instrumentation, works with wasm EH): Emscripten's fibers only exist for Asyncify
  (`emscripten_fiber_swap` has no JSPI implementation), and libco needs them.
- Emscripten dynamic linking (MAIN_MODULE/SIDE_MODULE + dlopen) for the RELs: not tried; static is what the task
  asked for, and side modules with Asyncify and per-load data reset would have been more moving parts.

## Not verified

- **Online play in the browser:** not built. pbnet is the C stub on the web (`PBNET_STUB=ON`), the Online menu
  can't host or join. iroh/tokio don't build for `wasm32-unknown-emscripten`, and `web-sys`/`wasm-bindgen` only
  target `wasm32-unknown-unknown`, so the WebRTC (opentennis `rtc.rs`) or relay-WebSocket transport needs Emscripten
  JS glue under pbnet's C ABI; `net/rtc.rs` (named in the brief) was not started. Follow-up: **N88**. And the
  browser/desktop divergence above has to be fixed first for mixed matches: **N91**.
- **No WebGL2 path:** aurora draws with WebGPU only and Dawn has no GL backend in the browser; without
  `navigator.gpu` the page says so and stops (N89).
- **Other browsers and devices:** only headless Chromium 154 on Linux with a real GPU. Not run: Firefox, Safari
  (OPFS without `createWritable` falls back to this visit only, untested), Chrome on Android (touch pad overlay via
  SDL's Emscripten touch events, memory with a whole disc in memory), a real visible window (resizing, fullscreen,
  devicePixelRatio). No machine or browser for them here. Follow-up: **N89**.
- **SwiftShader:** Chromium headless's default (no GPU) loses the device on the canvas's second texture; not worked
  around (it's the environment: a plain WebGPU page does the same). A real browser without a GPU would show the
  game stopped.
- **Saves across reloads:** IDBFS is mounted and synced (every 5 s, `pagehide`), and config.json was written
  ("Saving config" in the log), but no run checked that a memory card save or the config is still there after a
  reload.
- **Gamepads, touch, audio:** gamepads (SDL's Emscripten joystick) and touch not tried (no device); audio is
  stubbed in every build.
- **Binding the keyboard to port 1 in Settings and playing a party by hand:** not done; the scripted party is the
  check that a party starts.
- **A full party to the end in the browser:** the run stopped at frame ~15500 (turn 2 of 10) after 200 s; the end
  (mstory3Dll, back to the menus) was not reached in the browser.
- **The whole Asyncify skip list's safety:** checked by the runs above (title, party, a minigame, REL links), not
  by reasoning over every listed function; a listed function that ever yields would break unwinding there
  (`tools/web_eh_list.py` explains why none should).
- **iOS/macOS/Windows/Android desktop builds:** not rebuilt; the shared changes are `EMSCRIPTEN`-guarded.
- **A clean checkout:** a from-scratch configure + build in a new build dir passed (~3 min on 16 cores, deps
  fetched), and the four aurora patches apply in order to a pristine `git archive` of the submodule and give the
  same tree; but both used this machine's emsdk and cargo caches, not a fresh machine.
