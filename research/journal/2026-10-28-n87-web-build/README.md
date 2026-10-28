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
    is what dlopen after dlclose gives the desktop (fresh .data/.bss). 463 KB of REL data in all.
  - `objdll.c` / `object.h`: an `__EMSCRIPTEN__` branch next to the dlopen one.
- **Asyncify and wasm exceptions.** Rust's std for `wasm32-unknown-emscripten` is built with wasm exception
  handling (nod's `__cpp_exception` references), so the link has `-fwasm-exceptions`, and then setjmp/longjmp has to
  be wasm's too (`-sSUPPORT_LONGJMP=wasm` everywhere: Emscripten refuses JS sjlj with wasm EH). Binaryen's Asyncify
  pass emits invalid code for a function with exception handling in it ("non-final block elements returning a
  value must be dropped"). No such function ever is on the stack when the game yields or switches fibers (Rust,
  libc++abi, libpng/freetype error paths, nlohmann's errors), so:
  - `partyboard_eh_probe` (same objects, `-sASYNCIFY=0 --profiling-funcs`) is linked first; `tools/web_eh_list.py`
    reads its `wasm-dis` and lists every function with `try`/`try_table`/`throw`/... (~750); that list is
    `ASYNCIFY_REMOVE`. It is too long for one argument (wasm-opt gets it as one: E2BIG), so emcc's response file
    names a second file binaryen reads itself (`@@file` works; emcc turns `.` `#` `?` in it into ` ` `&` `,`, so the
    build dir's path can't have them, checked at configure).
  - The partyboard link stops at `-O1`: wasm-opt's -O2+ inline before Asyncify runs, and inlining a removed
    function into an instrumented one would put exception handling back into it. `tools/web.sh` runs
    `wasm-opt -O2` on the result.
  - The C/C++ is compiled without exception handling (Emscripten's default: a throw aborts unless caught...). The
    port catches C++ exceptions in `config.cpp` (a missing config.json is a `std::system_error`), `achievements.cpp`
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

## Checks

`tools/web_check.mjs DISC OUT [SECS] [QUERY]`: serves `build/web`, starts headless Chromium (`--headless=new
--enable-unsafe-webgpu`), picks DISC with `DOM.setFileInputFiles`, screenshots, console in `OUT/console.txt`.

RESULTS-PLACEHOLDER

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

NOTVERIFIED-PLACEHOLDER
