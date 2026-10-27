# N14: netplay time/pad hooks on Apple and Windows

## What was done

- **Route chosen: preprocessor rename.** `cmake/netplay.cmake` now has two routes, chosen by the cache
  variable `PB_NETPLAY_HOOK`:
  - `wrap` (default on Linux/Android): N4's `ld --wrap`, unchanged.
  - `define` (default on Apple and WIN32): compile definitions `OSGetTime=__wrap_OSGetTime`,
    `OSGetTick=__wrap_OSGetTick`, `PADRead=__wrap_PADRead` on `dol`, and the first two on every REL.
    The preprocessor renames every call and also the declarations in aurora's `os.h`/`pad.h`, so the
    game, port and REL objects call `netplay.c`'s `__wrap_*` directly. This works with any toolchain
    (ld64, link.exe, MinGW).
  - `wrap` on Apple/WIN32, or an unknown value, is a configure-time FATAL_ERROR.
- **Why the define route is enough.** `--wrap` also redirects calls from aurora's own objects. The define
  route does not, because aurora is not compiled with the definitions. But aurora calls none of the
  three except `OSGetTick` -> `OSGetTime` inside `OSTime.cpp` (grep over `extern/aurora`, excluding
  dawn/third_party). That call is already behind the wrapped `OSGetTick`.
- **`src/port/netplay.c`.** Under `PB_NETPLAY_DEFINE`, it `#undef`s the three names, redeclares aurora's
  real functions and maps `__real_X` to `X`, so the wrapper bodies are shared by both routes. The
  `__wrap_*` functions are `__declspec(dllexport)` on `_WIN32`, so the RELs can import them from dol
  without touching `dol.def`. That file has a hand-kept EXPORTS list with `OSGetTime`/`OSGetTick`, but not
  the wrappers. dllexport plus /DEF together is fine for link.exe.
- **Alternatives rejected:**
  - A hook pointer in aurora's OSTime.cpp/pad.cpp means a submodule change in another repo, for no gain.
  - ld64 `-alias` cannot rebind references inside one image.
  - dyld `__interpose` only works across images, and aurora is static inside libdol.
  - objcopy symbol renames don't exist for Mach-O/COFF in the toolchains used there.

## Verified (Linux, both routes)

- `tools/check.sh` passes, which covers the default wrap route.
- `cmake --preset linux-default-relwithdebinfo -B build/n14-define -DPB_NETPLAY_HOOK=define` builds and
  links cleanly. In that build:
  - `nm -u` over all 374 objects: only `netplay.c.o` references the real
    `OSGetTime/OSGetTick/PADRead`.
  - Every REL `.so` imports only `__wrap_OSGetTime`/`__wrap_OSGetTick` (for example `bootDll.so` ->
    `__wrap_OSGetTick` and `modeseldll.so` -> `__wrap_OSGetTime`).
  - `BoardRandInit` calls `__wrap_OSGetTime@plt` and `PadReadVSync` calls `__wrap_PADRead@plt`.
- The set of (object, symbol) call sites is identical in the wrap build and the define build: 9 pairs,
  diffed.
- The define build boots with `--script tools/netplay/mash-a.script`, logs the script load and
  `session start`, then waits in the prelaunch UI (no disc).

## Tried / failed

- Nothing failed. As in N4, the smoke run needs `timeout -s KILL` with output written to a file.

## Not verified

- **No macOS/iOS/Windows build was done**: this machine has only Linux toolchains (no Xcode, no MSVC).
  The define route is plain C preprocessor use, and it was tested on Linux as described above. Not
  checked:
  - That MSVC accepts `/DOSGetTime=__wrap_OSGetTime` via CMake (it should, since CMake generates the
    `/D` flags).
  - That the `__declspec(dllexport)` wrappers end up in dol.lib and resolve from the REL DLLs.
  - That ld64 resolves the RELs' `__wrap_*` against libdol.dylib.
  - MinGW: GNU ld for PE also supports `--wrap`, but WIN32 always uses the define route here.
- Determinism across peers on those platforms, and "two runs give identical hash logs" in the real
  game on any platform: there is no GMPE01 disc image in `orig/GMPE01_00/`, so no game frame ran
  (same blocker as N4, run in N15).
- Android was not built here. It still uses the wrap route, unchanged from N4.
