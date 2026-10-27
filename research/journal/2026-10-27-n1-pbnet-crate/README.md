# N1: `pbnet` crate linked into `dol`

## What was done

- `net/` crate `pbnet` 0.1.0, `crate-type = ["staticlib", "rlib"]` (rlib so `cargo test` can run its unit
  tests), no dependencies, `panic = "abort"` in release so nothing unwinds across the C ABI.
- One export: `const char *pbnet_version(void)`, a static NUL-terminated copy of `CARGO_PKG_VERSION`.
  Safety contract documented on the fn; two unit tests (value matches the package version, pointer stable).
- Header `include/port/pbnet.h` is hand-written (one fn; cbindgen would be a build dependency for nothing yet).
  Keep it in sync with the `extern "C"` fns in `net/src/lib.rs`.
- CMake: `cmake/pbnet.cmake` (new file, keeps the `CMakeLists.txt` diff to two lines) runs
  `cargo build --release --locked` into `${CMAKE_BINARY_DIR}/pbnet-cargo` from an always-run custom target
  with the `.a` as a BYPRODUCT, and wraps it as the imported static library `pbnet::pbnet`, linked
  PRIVATE into `dol`. Cargo does its own change tracking; Ninja's restat stops `dol` relinking when the
  crate did not change. Chose `add_custom_target` over Corrosion: no extra FetchContent download, and
  the cross-target mapping is a few lines.
- **Two Rust std copies in one link.** aurora already links nod's prebuilt Rust staticlib
  (`_deps/nod_prebuilt-src/lib/libnod.a`) into `dol`. Every Rust staticlib carries its own std, so the
  first link of `libdol.so` failed: `multiple definition of 'rust_eh_personality'`. They are also built by
  different rustc versions, so `--allow-multiple-definition` would be unsafe. Fix (ELF): pre-link the cargo
  archive with `ld -r --whole-archive` into one object, `objcopy --keep-global-symbol=pbnet_*` to make
  everything else local, then re-archive as `pbnet-cargo/libpbnet_hidden.a`. Any future pbnet export
  must start with `pbnet_` or it gets hidden too.
- `PBNET_CARGO_TARGET` cache var (empty = host). Auto-mapped for Android ABIs, iOS (device/simulator) and
  single-arch macOS. Native link libs per platform from `rustc --print native-static-libs` for a std-only crate.
- `port_main` (`src/port/portmain.cpp`) logs `pbnet <version>` right after `aurora_initialize`, before
  the DVD image is checked.
- `net/Cargo.lock` is committed (needed by `--locked`); `net/target` is git-ignored.

## Tried / failed

- First `tools/check.sh` run failed at configure: the worktree had no submodules checked out
  (`extern/aurora`, `extern/musyx`, `extern/libco`). `git submodule update --init --recursive` fixed it;
  nothing to do with this change.
- Localizing alone was not enough: the link then failed with `relocation R_X86_64_PC32 against undefined
  hidden symbol 'DW.ref.rust_eh_personality'` from libnod.a. That symbol sits in a COMDAT group; the linker
  kept pbnet's copy of the group (now with a local symbol) and dropped nod's, so nod's reference had
  nothing to resolve to. `objcopy --remove-section=.group` (it is the only group in the object) turns it
  into a plain local section, and both copies coexist.

## Verified

- `tools/check.sh`: 2 cargo tests pass, desktop Linux build links.
- `nm -D libdol.so`: `pbnet_version` exported; none of pbnet's std symbols leak.
- No-op rebuild runs cargo (`Finished ... in 0.00s`) but does not relink `dol`.
- Ran `./partyboard` without a disc image (killed after 12 s at the prelaunch UI): log shows
  `[INFO | partyboard::main] pbnet 0.1.0`. The line is logged before the DVD check, so no disc was needed.

## Not verified

- Windows/MSVC: the cargo `.lib` is linked as is, no symbol hiding. Whether it clashes with libnod there is
  untested (no Windows machine here). Follow-up N13.
- macOS/iOS: the `xcrun ld -r -exported_symbol "_pbnet_*"` pre-link and the target mapping were never run
  (no Apple toolchain here). Follow-up N13.
- Android: the target mapping and `dl log m` link libs were never built (no NDK build here). Follow-up N13.
- Full gameplay with pbnet linked in: not run (no GMPE01 disc image in `orig/GMPE01_00/`). pbnet does
  nothing but return a string, so this only matters if linking it changed something else.
