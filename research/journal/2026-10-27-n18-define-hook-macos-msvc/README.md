# N18: netplay define hook route on real macOS and MSVC

**Blocked**: this machine is Linux only. There is no Xcode/macOS SDK, no MSVC or Windows SDK (only
clang-cl without headers/libs, MinGW and wine), and no GMPE01 disc image in `orig/GMPE01_00/`. The fork's
GitHub Actions (which has macOS and Windows jobs in `.github/workflows/build.yml`) is enabled but has
never run on FIZL0/partyboard (`gh run list` is empty, even with task branches pushed), so CI is no
substitute either. Fetching the MSVC SDK with xwin means accepting Microsoft's license unattended, so
that was not done.

## What was done

- **`tools/netplay/check-hook-imports.sh <build dir>`**: the import check from the task as one command
  that works on all three formats (llvm-nm/llvm-objdump, falls back to nm/objdump):
  - dol (`libdol.so`, `libdol.dylib`, `dol.dll`/`libdol.dll`) must export `__wrap_OSGetTime`,
    `__wrap_OSGetTick` and `__wrap_PADRead`.
  - No REL (`*Dll.{so,dylib,dll}`) may import the bare `OSGetTime`/`OSGetTick`.
  - At least one REL must import a wrapper.
  - Mach-O's leading underscore is stripped only for `.dylib`. PE imports are read from
    `llvm-objdump -p` (Hint/Ord tables) and exports from its Export Table. `VERBOSE=1` lists each REL.
- Checked on:
  - Linux, define route (`build/n18-define`, `-DPB_NETPLAY_HOOK=define`, builds cleanly): ok, 87 RELs,
    3 import the wrappers.
  - Linux, wrap route (`build/linux-default-relwithdebinfo`): ok, same 3 RELs.
  - Toy PE DLLs built with MinGW: a "dol" with `__declspec(dllexport)` wrappers and a REL compiled with
    `-DOSGetTime=__wrap_OSGetTime ...`. That passes. A REL built without the defines (dol built with
    `--export-all-symbols` so it links) fails, listing `OSGetTick OSGetTime`.
  - Toy Mach-O arm64 dylibs (clang `-target arm64-apple-macos11`, linked with rustup's
    `rust-lld -flavor darwin`, `-undefined dynamic_lookup` because there is no libSystem): same pass and
    fail.
- Smoke run of the define build with `--script tools/netplay/mash-a.script --hash-log`: logs the script
  load and `session start, seed 50424E54`, and the hash log stays empty. With no disc it never leaves
  the prelaunch UI, so no frame runs and no frame-derived time can be observed (same as N14).
- Read through the Apple/MSVC paths in `CMakeLists.txt` and found nothing that should break the define
  route:
  - On Apple, the RELs link `dol` directly, and their `-exported_symbols_list rel_symbols.txt` limits
    only what the REL exports, not what it imports.
  - On MSVC, `dol.def` lists `OSGetTime`/`OSGetTick` (aurora's real ones), and the wrappers come from
    `__declspec(dllexport)` in `netplay.c`. The two lists don't overlap, so there should be no LNK4197.

## For whoever has the machines

    # macOS
    cmake --preset macos-default-relwithdebinfo && cmake --build --preset macos-default-relwithdebinfo
    tools/netplay/check-hook-imports.sh build/macos-default-relwithdebinfo   # needs llvm-nm or Xcode nm
    # Windows (Developer PowerShell, then Git Bash for the script, with LLVM on PATH)
    cmake --preset windows-msvc-relwithdebinfo && cmake --build --preset windows-msvc-relwithdebinfo
    tools/netplay/check-hook-imports.sh build/windows-msvc-relwithdebinfo
    # both, with a disc in orig/GMPE01_00/: two runs, identical hash logs
    partyboard --script tools/netplay/mash-a.script --hash-log run1.log
    partyboard --script tools/netplay/mash-a.script --hash-log run2.log
    tools/netplay/hashdiff.py run1.log run2.log

## Tried / failed

- Getting a Mach-O linker: there is no `ld64.lld` or `lld` package. `rust-lld -flavor darwin` works,
  but without an SDK it needs `-undefined dynamic_lookup` (otherwise `dyld_stub_binder` is undefined).
  That is fine for a toy, but not enough to link the real game.

## Not verified

- **macOS build of the define route**: no macOS SDK/Xcode here. Not checked whether ld64 links the RELs
  against `libdol.dylib` with `___wrap_*`, or whether the checker's `llvm-nm -u -D` call behaves the same
  on Apple's `nm`. It was only checked on rust-lld output.
- **MSVC build**: no MSVC/Windows SDK. Not checked whether the `/D` definitions reach the MSVC command
  line, whether `__declspec(dllexport)` plus `/DEF:dol.def` lands the wrappers in `dol.lib`, or whether
  the REL DLLs import them. The PE parsing in the checker was only checked on MinGW output (the
  `llvm-objdump -p` format is the same for link.exe images, but that wasn't seen here).
- **"--script run logs frame-derived time"** on any platform: there is no disc image, so no game frame
  runs. Also, nothing in `netplay.c` logs OSGetTime values today. The hash log is what would show
  frame-derived time once a disc is present.
- The fork's GitHub Actions never ran, so the CI macOS/Windows jobs give no evidence either.
