# N91: browser vs desktop determinism

N87 found that `tools/netplay/party.script --match 0@1500 --turbo --hash-log` gives the same per-frame hashes on the
web build (wasm32, clang, musl) and the Linux build (x86_64, GCC, glibc) up to frame 10868, then `plr3` differs at
10869 inside m415Dll. The guess was libm (`sind`/`cosd`/`atan2d` on double `sin`/`cos`/`atan2`).

## What it was: a float -> u8 conversion out of range in m415, not libm

- **libm is not it (for this party).** With the game's math swapped for musl's (below), the Linux build's hash log
  equals the glibc build's for all 135000 frames (`hashdiff.py`: identical, heap= included), through the whole party
  and `match over` at 110988. glibc's and musl's sin/cos/atan2/sinf/... give the same bits on every argument this
  party passes, so the 10869 split had another cause.
- **The cause:** a Linux build with GCC's `-fsanitize=float-cast-overflow` on all C (`tools/netplay/float_cast_check.sh`)
  ran the same party and reported three sites, two of them in m415 at the frame the logs split:
  ```
  src/REL/m415Dll/main.c:1196:95: runtime error: -1.73719 is outside the range of representable values of type 'unsigned char'
  src/REL/m415Dll/main.c:1197:95: runtime error: -2 is outside the range of representable values of type 'unsigned char'
  extern/aurora/include/dolphin/os/OSFastCast.h:15:1: ... type 'unsigned char'   (OSf32tou8, board fade alpha)
  ```
  m415 (the paint minigame) `fn_1_...` case 1, a COM painter picking its next cell: `temp_r28 = (u8)(((0.5f * (600 +
  x)) - 150 * cell) / 50.0f)` goes negative when the painter stands left of/above its target block. Converting a
  negative float to `u8` is undefined in C. GCC on x86_64 does `cvttss2si` (to s32) and keeps the low byte: -1.7 ->
  -1 -> 255, the loop over `temp_r28 - 1 .. temp_r28 + 1` (254..256) finds no cell in 0..2, nothing happens. clang
  on wasm32 converts with `i32.trunc_sat_f32_u` (unsigned, saturating): -1.7 -> 0, the loop looks at cells 0 and 1,
  and the painter's choice (and its `fn_1_679C` draws, m415's own generator, not one of the three hashed ones) differ:
  another stroke, other paint counts, another winner (`GWPlayer[3].coin_win`, the first hashed difference at 10869).
  The GameCube does what x86 does (`fctiwz` to s32, `stb` the low byte), so the desktop was the faithful one.
- **Fix:** convert through `s32` first, `(u8)(s32)f`, which is defined and wraps the same on every platform (and on
  the GC). m415 main.c's two lines, and aurora's `OSf32tou8`/`tos8`/`tou16`/`tos16` (patch 0009, they had the same
  undefined `(u8)inF`; the board's fade filter hit it). The desktop's behaviour is unchanged (it already wrapped).

## libm anyway: one implementation for the game everywhere

The brief asked for this and it is still a real risk for other parties and platforms: bionic (Android), Apple's and
Microsoft's libms are other implementations than glibc and musl, and only sqrt/fmod/floor/... are exact by the
standard. So:

- `src/port/math/`: musl's math (the copy Emscripten 6.0.13 ships, musl 1.2.6, i.e. the web build's own libm): sin
  cos tan asin acos atan atan2 exp log pow and the float sin..atan2f, with their kernels (`__sin`, `__rem_pio2`, ...)
  and the exp/log/pow tables. The .c files are verbatim; `libm.h` is ours (what they need from musl's internal
  `libm.h` + `features.h`), it renames the internals `pb__*` (the web build links musl's libc.a next to them), drops
  `__FP_FAST_FMA` (log/pow take another path with FMA) and refuses `FLT_EVAL_METHOD != 0` (`__rem_pio2`'s rounding
  trick). The `*_data.h` files include "libm.h" instead of `<features.h>` (the only edit). MIT, `COPYRIGHT.musl`.
- `src/port/math/include/math.h` is first on the include path of the game's C (dol, every REL, aurora's mtx:
  `cmake/pbmath.cmake`): `#include_next <math.h>`, then `pb_math.h`, which `#define`s `sin` -> `pb_sin` etc. After
  the system header, so glibc's declarations (its SIMD `__DECL_SIMD_sin` token pasting broke when the macros came
  first) and a file's own `_GNU_SOURCE` (netplay_match.c's `dladdr`, broken by a forced `-include`) are untouched.
  C only: the C++ port code keeps `std::sin`. MSVC (no `#include_next`) gets `pb_math.h` forced in with `/FI`, where
  it includes `<math.h>` itself.
- `-ffp-contract=off` on the same targets: clang fuses `a*b+c` into FMA on arm64 (Android, iOS) by default; x86_64
  (no -march) and wasm32 have no FMA to fuse with, so they already agreed.
- libdol exports `pb_sin` ... (the RELs and the statically linked mtx call them there); the internals are hidden.

## Also fixed: the web build's CMake hooks

The N86 merge (a86810a5) dropped N87's lines from CMakeLists.txt: `PB_DOL_LIBRARY_TYPE`/`PB_REL_LIBRARY_TYPE` and the
`include(cmake/web_early.cmake)` / `include(cmake/web.cmake)` under `if (EMSCRIPTEN)`. `tools/web.sh` then failed in
fmt (web_early's `-includecstdlib` missing). Restored as they were (main has the same loss, N90's merge didn't touch
them).

## Checks

(filled in below as the runs finish)

## Tried and dropped

- `-include pb_math.h` (forced first, with `<math.h>` included inside it): netplay_match.c defines `_GNU_SOURCE`
  before its includes; a forced `<math.h>` came first and pulled in `<features.h>` without it (`Dl_info` unknown).
- The same header without `<math.h>` (macros before the system header): glibc's `bits/mathcalls.h` pastes the
  function name into `__DECL_SIMD_##name` after expanding it, `__DECL_SIMD_pb_sin` is no macro, syntax errors.
- The decomp's own MSL fdlibm (src/MSL_C.PPCEABI.bare.H, the GC's libm) instead of musl: big-endian word access
  (`__HI(x)` = `*(int*)&x`), no float functions; musl is the same fdlibm lineage, portable, and is already what the web
  build runs.

## Not verified

(filled in below)
