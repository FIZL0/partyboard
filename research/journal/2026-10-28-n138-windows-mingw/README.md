# N138: Windows release build cross-compiled with MinGW

`tools/build.sh windows` builds `build/dist/partyboard-windows-x86_64.zip` on this Linux box: a `PartyBoard\`
folder with `partyboard.exe`, `libdol.dll`, the 87 REL DLLs, aurora's DLLs (SDL3, webgpu_dawn + dxcompiler/dxil,
nod) and `res\`. 41 MB zipped. Under wine it boots with `PB_DISC`, runs a `--script` to its end, and a `--match`
run's hash log is **identical to the Linux build's for all 17400 frames** (hashdiff.py). `tools/release.sh`
builds and uploads it, with an opentennis-style row.

## What the build is

- `CMakePresets.json`: configure + build preset `windows-mingw` (RelWithDebInfo, -O2 like the Linux build;
  `tools/build.sh` strips). `BUILD_SHARED_LIBS=OFF` (abseil, zlib, png, freetype, rmlui static into dol),
  `AURORA_NOD_PROVIDER=vendor` with `Rust_CARGO_TARGET=x86_64-pc-windows-gnu`, PkgConfig off (it found the
  host's zstd and leaked `/usr/include` into the cross build).
- `cmake/toolchains/x86_64-w64-mingw32.cmake`: MinGW GCC 16, `CMAKE_SYSTEM_PROCESSOR=AMD64` (so aurora takes its
  windows-amd64 Dawn and SDL3 packages), `-static-libgcc -static-libstdc++` + whole-archive winpthread on every
  link, `PBNET_CARGO_TARGET=x86_64-pc-windows-gnu`, `_WIN32_WINNT` 10 without the NI WinRT headers (abseil's
  time zone code doesn't compile against MinGW's `windows.globalization.h`), `mingw-include/` (aurora's MSVC
  header spellings `Windows.h`, `Wbemidl.h`, `shlobj_core.h`) and `mingw-lib/` (linker scripts for the MSVC
  library names aurora links: `comsuppw`, `DXGI`).
- No MinGW runtime DLL ships: `tools/build.sh` checks that every DLL any binary imports is in the folder or a
  Windows one. The imports outside Windows' own are MSVCP140/VCRUNTIME140(_1) — from **aurora's prebuilt Dawn,
  DXC and SDL3** (MSVC builds), not from our code: the player needs the Visual C++ 2015-2022 x64 runtime, as
  with the MSVC preset. The release row links vc_redist.x64.exe. Our binaries import only Windows DLLs and the
  UCRT (`api-ms-win-crt-*`, in Windows 10).
- pbnet: cargo `--target x86_64-pc-windows-gnu`, linked as is (as on MSVC). The `ld -r` + localize prelink the
  ELF route uses fails on COFF (duplicate raw-dylib import stubs), and isn't needed: nod is its own DLL, so dol
  has one Rust std. No crt-static flag: the crate is a staticlib, the final link is GCC's with the static
  runtime flags above.
- Icon: `platforms/windows/png2ico.py` (Pillow) instead of the PowerShell script when the host isn't Windows.
- Netplay hook: the define route (N14), unchanged. `check-hook-imports.sh` on the package: ok, 87 RELs, 3 import
  the wrappers (same as the Linux build).

## What it took (in the order the build failed)

1. **dol exported 214 symbols.** The game's data is `__declspec(dllexport)` (SHARED_SYM, include/version.h),
   which switches MinGW's auto-export off for everything else. dol now links with `--export-all-symbols`
   (`--exclude-libs,libpbnet.a` keeps Rust's std out): 16.4k exports, well under PE's 65535.
2. **RELs and aurora.** On ELF a REL links aurora's mtx/gx static archives but resolves against libdol.so's copy
   at run time (one GX state). PE resolves at link time, so each REL would have had its own aurora. MinGW RELs
   link only dol and import aurora/musyx from it (their headers via the targets' include dirs). dol takes all of
   `libaurora_mtx.a` (whole-archive): m417 calls quaternion functions dol doesn't, and pulling them from the
   archive in the REL needed dol's `pb_sinf` after the import library had been read.
3. `partyboard.exe`: SDL's MinGW entry is `WinMain` in aurora_main's archive, read before libmingw32 asks for
   it: `-u WinMain`.
4. `HeapWalk` (netplay_match.c's struct) is a Win32 API function: renamed `NetplayHeapWalk`.
5. RELs are named `bootDll.dll`, not `libbootDll.dll` (objdll.c loads `<name>.dll`): `PREFIX ""`.
6. **Dawn's C++ descriptor.** aurora's gpu.cpp constructs `dawn::native::DawnInstanceDescriptor`, whose
   out-of-line constructor the DLL exports only MSVC-mangled. `src/port/mingw/dawn_native.cpp` defines it for
   GCC's name (Dawn's sets sType only). It linked, then crashed in Dawn: GCC (Itanium ABI) puts the derived
   struct's first `uint32_t` into `wgpu::ChainedStruct`'s tail padding, MSVC doesn't, so Dawn read every field
   8 bytes off: `backendValidationLevel` (Disabled = 2) as the platform pointer, "read access to
   0000000000000002", and with Vulkan "Backend validation enabled". webgpu_cpp.h's own chained structs
   `alignas` their first member against exactly this; DawnNative.h's doesn't. `cmake/patches/aurora/0015`
   leaves the descriptor out under `__MINGW32__` (Dawn's default validation is off). Its one hunk sits after
   0004's `#endif`; every patch still reverse-checks.
7. **bootDll jumped to `0x7000231a0000`.** aurora's os.h declares `OSReport`/`OSPanic` with `DECL_WEAK`. An
   undefined weak symbol on ELF is bound from libdol.so at load time; a COFF weak external never pulls the import
   library's stub, so the REL's `call OSReport` linked to 0 (`call 400000000` in the disassembly, no pseudo
   relocations at all). The fault was on a libco stack, outside the thread's TIB stack limits, so neither wine
   nor Windows could dispatch it: no crash report, the process just vanished (wine: "Exception frame is not in
   stack limits"). Found with gdb on wine inside the headless gamescope (`harness/segv.gdb`: stop at the first
   SIGSEGV with a page-aligned pc, dump registers, stack, maps) and `WINEDEBUG=+loaddll` for module bases. Fix:
   `DECL_WEAK=` for dol and the RELs on MinGW (aurora's own weak defaults, compiled without it, still lose to
   dol's definitions; dol's definitions are then strong and exported).
8. **The match start killed 4 processes, Linux 2**, and the board never loaded (ovl -1 to the end).
   `code_module()` (netplay_match.c) asks `GetModuleHandleExA(FROM_ADDRESS)` for a process's func and dtor, and
   with NULL that returns the .exe's module: every dol process without a destructor counted as the menu's. A
   Windows bug in the existing code (MSVC builds too). NULL now has no module. Also there: an import thunk
   (`FF 25`, `jmp *__imp_fn(%rip)`) is followed to its IAT target, since a REL taking a dol function's address
   gets its own thunk (ELF has one canonical address). That case did not show up in this run (the debug print
   listed real addresses only); it is there because a REL's `HuPrcCreate(dolFn)` would hit it.

## Checks

All under wine 11.20 in `gamescope --backend headless` (nothing on the desktop), one game at a time, prefix and
runs under `.n138-runs/` (`harness/runwine.sh`; a fresh `MarioPartyRD` AppData per run). The unpacked zip, not
the build dir.

- `--script tools/netplay/mash-a.script --turbo` with `PB_DISC` (Vulkan): boots, loads the disc, goes through
  the menus, the board (w01), a minigame and the results; `script ended at frame 17400`.
- Same run with `--match 0@1500`, Linux build vs Windows build: **`identical: 17400 frames`**, overlays
  `-1@0, 1@1, -1@993, 74@995, -1@1500, 89@1502, -1@8205, 3@8206, -1@10606, 16@10607, -1@13488, 84@13489,
  -1@13772, 89@13773`, both `hash 35F0AB781EC2B38C`, both "killed 2 menu processes".
- Offline (no `--match`) the two match for 15391 frames, then wine leaves overlay 84 (after the minigame) 139
  frames earlier. Two Linux runs are identical with each other. Offline sound statuses follow the audio device's
  clock (N112/N122) and wine runs at another speed, so the frame a sound ends on differs; `--match` runs are
  frame-locked and match. Not a build difference.
- No `PB_DISC`, no script: the prelaunch screen ("SELECT DISC IMAGE") renders (gamescopectl screenshot).
- `tools/netplay/check-hook-imports.sh build/dist/windows/PartyBoard`: ok (run by tools/build.sh).
- `tools/check.sh`: passes (the Linux build with these changes; the MinGW parts are `if (MINGW)`).
- A from-scratch `tools/build.sh windows` (empty build dir, ~40 min at CPUWeight=1): the same 119 files as the
  incremental build, and its zip's `--match` run is again identical to Linux for 17400 frames.

## Wine-only problems (not the build)

- Dawn's D3D12 under wine: "D3D12 create fence handle failed with E_NOTIMPL" (vkd3d has no shared fence
  handles). Then Vulkan and D3D11: Dawn's `LoadLibraryEx` of `vulkan-1.dll` / `d3dcompiler_47.dll` from system32
  fails with Windows error 87 under wine (its first try, the exe's own folder, works). So the default backend
  order finds no device under wine. The wine runs above set `"backend.graphicsBackend": "vulkan"` in config.json
  and put wine's `vulkan-1.dll` beside the exe (test only, not in the zip).
- wine leaves its services running, so gamescope never exits after the game: the runs end at their `timeout -s
  KILL` and `wineserver -k`.

## Not verified

- **A real Windows 10/11 machine** (N18, a human check): D3D12 and Vulkan through Dawn's normal loading, the
  Visual C++ runtime prompt when it's missing, the disc picker dialog (SDL's Windows file dialog), controllers,
  audio devices, online play to another machine. None of it can run here.
- Online play between the Windows build and another peer: only the single-machine `--match` lockstep path ran.
- A machine without the VC++ runtime: wine ships its own msvcp140/vcruntime140, so the missing-runtime case
  can't happen here. If it bothers players, the alternative is a MinGW (vendor) Dawn build, not tried.
- Cross-module function pointer comparisons in game code other than netplay_match's (a REL's thunk vs dol's real
  address): none known, none looked for.
- The .exe is a console-subsystem program, as the MSVC preset's is (`WIN32_EXECUTABLE` is commented out
  upstream): on Windows a console window opens behind the game. Not changed.
- ld warns ".rsrc merge failure: duplicate leaf: type: 10 (VERSION)" linking partyboard.exe: winpthread's own
  version resource comes in with the whole-archive link. The exe keeps ours (checked: its strings have
  "Party Board", "Mario Party R&D", "partyboard.exe"); every DLL carries winpthread's version info. Cosmetic.
