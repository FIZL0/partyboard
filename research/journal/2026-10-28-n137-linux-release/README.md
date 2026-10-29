# N137: Linux release build (partyboard-linux-x86_64.tar.gz)

User 2026-10-28: "for our releases can we publish linux and windows builds aswell? match opentennis". This is the
Linux half (N138 is Windows).

## What it is

- `tools/linux-release.sh [out.tar.gz]` (default `build/release/partyboard-linux-x86_64.tar.gz`): it builds the
  `linux-release` preset inside Valve's Steam Runtime **sniper** SDK container (Debian 11, glibc 2.31, `gcc-14`), as
  opentennis's `tools/build.sh linux` does, then puts together `PartyBoard/`: `partyboard`, `libdol.so`, every REL
  `.so` (91), `libSDL3.so.0`, `res/` and `README.txt` (where the disc goes), stripped with `--strip-unneeded`.
  Unpacked it is 76 MB and the tarball is 33 MB.
- After the build the script refuses a folder that:
  - needs a library from the system other than libc/libm/libdl/libpthread/librt/libutil/ld-linux (each file's
    NEEDED);
  - has a runpath other than `$ORIGIN`;
  - needs a glibc newer than 2.31 (`objdump -T`; today it needs GLIBC_2.30).
- `tools/release.sh` calls it and uploads the tarball (its own `files+=` line, so N138's line merges cleanly). It also
  has the opentennis-style row: *Linux / Steam Deck (x86_64, glibc 2.31+: Ubuntu 20.04+, Debian 11+, SteamOS 3):
  unpack and run `PartyBoard/partyboard`, then pick your disc image*.
- The `linux-release` preset (CMakePresets.json) makes these changes from `linux-default-relwithdebinfo`:
  - `AURORA_SDL3_PROVIDER=vendor` and `AURORA_SDL3_LINKAGE=shared`. SDL3 is built from source as libSDL3.so.0 next to
    the binary. Shared keeps one SDL in the process: `partyboard` and `libdol.so` both link it. SDL dlopens X11,
    Wayland, libdecor, ALSA, Pulse, PipeWire, udev and dbus itself (`SDL_*_SHARED=ON`, checked in CMakeCache.txt).
  - `BUILD_SHARED_LIBS=OFF`, so RmlUi, fmt, abseil and freetype are static. The default build ships librmlui.so.
  - `CMAKE_DISABLE_FIND_PACKAGE_{absl,PNG,Freetype,SQLite3,zstd,fmt}`, so aurora fetches each one.
  - `ZLIB_USE_STATIC_LIBS=ON`, which uses sniper's libz.a.
  - `FT_DISABLE_BZIP2` (the fetched freetype had picked up the system libbz2.so).
  - `-static-libstdc++ -static-libgcc`, and `PARTY_BOARD_GLIBC_COMPAT=ON` (see below).
  - The build type stays RelWithDebInfo (-O2), the code generation the dev builds are tested with. The binaries are
    stripped afterwards.
- Docker state: the Rust toolchain and cargo cache are in `~/.cache/partyboard-steamrt`, the build is in
  `build/linux-release`, the install in `build/linux-release-install` and the folder in `build/linux-release-dist`.
  A worktree's `.git` points into the main checkout, so the script mounts the git common dir at its own path too
  (configure's version stamp and aurora patches need git).

## What failed on the way

1. `CMAKE_DISABLE_FIND_PACKAGE_ZLIB` stops the configure: the fetched libpng calls `find_package(ZLIB REQUIRED)`,
   and CMake refuses to disable a REQUIRED package. I switched to `ZLIB_USE_STATIC_LIBS`. Sniper's libz.a has no
   `R_X86_64_32` relocations, so it links into the shared libdol.so.
2. The link fails on aurora's prebuilt Dawn (`_deps/dawn_prebuilt-src/lib/libwebgpu_dawn.a`, the
   `AURORA_DAWN_PROVIDER=package` default). It was compiled on a newer distro and references
   `__isoc23_strto{l,ll,ul,ull}` (glibc 2.38), `__libc_single_threaded` (glibc 2.32) and
   `std::ios_base_library_init()` (GCC 13's libstdc++; sniper's `gcc-14` libstdc++.a doesn't define it).
   The fix is `src/port/glibc_compat.c` (only with `PARTY_BOARD_GLIBC_COMPAT`, off by default), linked into dol, all
   hidden:
   - the strto* stand-ins forward to the C17 functions;
   - `__libc_single_threaded` is 0, so libstdc++ always takes the atomic path;
   - `ios_base_library_init` is empty (it only pulls libstdc++'s stream init into the link).

   The other route is `AURORA_DAWN_PROVIDER=vendor`: a few thousand Dawn TUs with gcc in the container, and a Dawn
   build that differs from every other platform's. I didn't take it. If a future Dawn prebuilt needs more symbols,
   the link names them, and the glibc check catches any versioned ones.

## Checks (all on HEAD f4ba95d6 + this change)

`check.sh` in this folder unpacks the tarball into `.n137-runs/<mode>/game` and runs it from a cwd outside the
folder, in a fresh user dir, with PB_DISC. It plays `tools/netplay/party.script`: a whole `--match 0@1500 --turbo`
party on w01.

| Where | Result |
|---|---|
| host (Arch, glibc 2.44), clean dir | `match over at frame 96948`, `script ended at frame 135000, hash 9EBD8E7B3E4B0645` (5 min) |
| Fedora 43 container (`pb-n137-fedora` = fedora:43 + mesa-vulkan-drivers, vulkan-loader, wayland/X11 client libs, alsa-lib, libdecor), RADV on the RX 9070, hidden window on the host's Wayland socket, SDL dummy audio | same frames, same final hash, hash log byte-identical to the host run |
| Debian 11 container (debian:bullseye, glibc 2.31 = the floor) | every NEEDED resolves (`ldd`, none "not found"), the game loads config and starts aurora, and stops at `Error initializing SDL: wayland not available` (the bare image has no libwayland-client) |

The Arch dev build (`build/linux-default-relwithdebinfo`, system libs, Arch gcc) on the same script gives the same
hash log through `match over` (96948) and on to frame 102011. The heap layout first differs at frame 102012, in
overlay 78 (after the match). An online session ends at `match over`, so a release peer and a self-built Arch peer
agree for the whole match. I didn't chase the post-match split: I didn't check whether two dev runs also split
there.

The log says `No bundled initial pipeline cache found at …/PartyBoard/initial_pipeline_cache.db`. That is an
error-level line, but no build makes that file (no cmake or tools rule writes it), so the dev build prints the same.

## Not verified

- **A real game on the glibc 2.31 floor.** The Debian 11 run stops at window creation. Installing Wayland and Mesa
  in debian:bullseye failed: the security mirror returns 404 for `mesa-vulkan-drivers 20.3.5-1+deb11u1` and expat.
  Bullseye's Mesa 20.3 has no RDNA4 (RX 9070) driver anyway. The floor rests on `objdump -T` (needs GLIBC_2.30),
  the loader resolving every library on 2.31, and the boot up to SDL video init. An Ubuntu 22.04 container with
  lavapipe, or a real older machine, would close this.
- **Steam Deck / SteamOS.** Not tried (no device). It is the same glibc floor and container that opentennis's Deck
  build uses.
- **X11-only sessions.** The Fedora run used Wayland. SDL's X11 backend is built in (dlopen) but wasn't exercised.
- **Audio in the release build.** The container run used `SDL_AUDIO_DRIVER=dummy`. The host run used the host's
  audio, but nobody listened.
- **The prelaunch disc picker in the release build.** Scripted runs take the disc from PB_DISC. The README's "pick
  your disc image" is the same prelaunch UI the dev build has.
- **`tools/release.sh` end to end.** Not run: it tags, pushes and creates a GitHub release, which this task must not
  publish. `bash -n` passes, and the Linux step it calls (`tools/linux-release.sh "$out/…tar.gz"`) is the run
  above.
