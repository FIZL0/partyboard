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
  ran the same party and reported four sites (GCC reports each source location once), two of them in m415 at the
  frame the logs split:
  ```
  src/REL/m415Dll/main.c:1196:95: runtime error: -1.73719 is outside the range of representable values of type 'unsigned char'
  src/REL/m415Dll/main.c:1197:95: runtime error: -2 is outside the range of representable values of type 'unsigned char'
  extern/aurora/include/dolphin/os/OSFastCast.h:15:1: runtime error: -1.33333 is outside ... 'unsigned char'   (OSf32tou8: board fade alpha)
  src/game/hsfdraw.c:482:49: runtime error: 1424 is outside ... 'unsigned char'   (a material's ambient color)
  ```
  m415 (the paint minigame) `fn_1_4660` (a painter object), state `unk5D == 1`, a COM painter picking its next cell: `temp_r28 = (u8)(((0.5f * (600 +
  x)) - 150 * cell) / 50.0f)` goes negative when the painter stands left of/above its target block. Converting a
  negative float to `u8` is undefined in C. GCC on x86_64 does `cvttss2si` (to s32) and keeps the low byte: -1.7 ->
  -1 -> 255, the loop over `temp_r28 - 1 .. temp_r28 + 1` (254..256) finds no cell in 0..2, nothing happens. clang
  on wasm32 converts with `i32.trunc_sat_f32_u` (unsigned, saturating): -1.7 -> 0, the loop looks at cells 0 and 1,
  and the painter's choice (and its `fn_1_679C` draws, m415's own generator, not one of the three hashed ones) differ.
  From there the paint counts differ; the first hashed field to move is `plr3` at 10869 (by the code, most likely
  `GWPlayer[].coin_win` set from the paint percentages at the end; not traced field by field).
- **What each compiler emits** for `unsigned char f(float x) { return x; }` (-O2): GCC x86_64 `cvttss2sil` (signed),
  NDK clang arm64 `fcvtzs` (signed), emcc wasm32 `i32.trunc_sat_f32_u` (unsigned, saturating). So for negatives only
  wasm differs. For `unsigned int g(float)` GCC x86_64 does `cvttss2siq` (64-bit signed: -1 -> 0xFFFFFFFF), arm64
  `fcvtzu` (saturating: -1 -> 0), wasm `i32.trunc_sat_f32_u` (0): there arm64 (Android, iOS) is the odd one out
  with x86, not wasm. Values above the type's range but inside s32 (hsfdraw's 1424) wrap alike everywhere.
  The GameCube does what x86 does (`fctiwz` to s32, `stb` the low byte), so the desktop was the faithful one.
- **Fix:** convert through `s32` first, `(u8)(s32)f`, which is defined and wraps the same on every platform (and on
  the GC). m415 main.c's two lines, aurora's `OSf32tou8`/`tos8`/`tou16`/`tos16` (patch 0009, they had the same
  undefined `(u8)inF`; the board's fade filter hit it) and hsfdraw.c's ambient color (render state only, and the
  same on every platform already, fixed so the check below reports nothing). The desktop's behaviour is unchanged (it
  already wrapped): its hash log equals the old build's for all 135000 frames.

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

## Also fixed: m418 traps in the browser

With m415 fixed, the browser party ran equal to the desktop up to m418 (turn 8, frame 82786), where the page died:
`RuntimeError: function signature mismatch` in `fn_1_9BA4` (`omMain` -> the object). Found from the console's wasm
stack (`wasm-opt -O2 -g --print-function-map` on the same input for the names; the `call_indirect` at the trap's
offset takes no argument). m418's sequence tables (`M418DllUnkStruct8`) hold `void (*)(s32)` / `s32 (*)(s32)`
functions, `fn_1_AE8C` casts them to `void (*)(void)` / `s32 (*)(void)` and `fn_1_5D58`/`fn_1_84B0`/`fn_1_9364`
call them with no argument. Native code passes whatever is in the register (none of the table functions reads
its `arg0`, so the desktop was deterministic); wasm checks the type on every indirect call and traps. Under
`TARGET_PC` the calls now go through `M418_SEQ_CALL`/`M418_SEQ_CALL_RET` (include/REL/m418Dll.h), which cast back to
the real type and pass 0; the GC build keeps the decomp's calls.

## Also fixed: two model hooks declared `(void)`

The next browser run (m418 fixed) was equal to the desktop for 88995 frames, through m418, and trapped the same way on
the board in turn 9 (player 1), in `Hu3DDrawPost`: a model hook (`HU3DMODELHOOK`, `(HU3DMODEL *, Mtx)`) that is a
`void (void)` function registered with a `(void *)` cast. A script over all 108 `Hu3DHookFuncCreate` calls (argument
-> its definition's parameter count) found two: the board Boo house's `BallRenderHook` (src/game/board/boo.c) and
w03's statue `fn_1_2B44`. Both now take `(HU3DMODEL *model, Mtx matrix)`, unused (the same code, the GC included).

A grep for other function casts (`(void *)fn`, `(SomeFunc)fn`) found more of the kind outside this party, fixed
without a run that reaches them:
- w02 roulette `fn_1_BE74` and shuffleboard `fn_1_94AC`: `s32 (void)` without a return, installed as
  `void (*)(void)` process destructors: now `void`.
- w05 mg_coin `fn_1_9B74`: `void (void)` installed as a post-turn hook `s32 (*)()` whose result
  `BoardPlayerPostTurnHookExec` reads (nonzero removes the hook). On the GC it returned `BoardModelAttrReset`'s r3,
  and the old desktop build did too (GCC tail-called it: `jmp BoardModelAttrReset@plt`, checked in the old
  w05Dll.so). On PC it now returns that value explicitly (`#ifdef TARGET_PC`, the GC build keeps `void`).
- mentDll `fn_1_81A8(obj, a, b)` installed as `MentDllUnkFunc` (two arguments): the unused third parameter dropped.
The rest (mentDll/ztardll handlers with typed pointer parameters: same wasm type, fine; others not audited) is
follow-up **N102**.

## Also fixed: the web build's CMake hooks

The N86 merge (a86810a5) dropped N87's lines from CMakeLists.txt: `PB_DOL_LIBRARY_TYPE`/`PB_REL_LIBRARY_TYPE` and the
`include(cmake/web_early.cmake)` / `include(cmake/web.cmake)` under `if (EMSCRIPTEN)`. `tools/web.sh` then failed in
fmt (web_early's `-includecstdlib` missing). Restored as they were (main has the same loss, N90's merge didn't touch
them).

## Checks

All with `tools/netplay/party.script --match 0@1500 --turbo --hash-log`, fresh user dirs, GMPE01 Rev 1, compared with
`tools/netplay/hashdiff.py`. Desktop = `build/linux-default-relwithdebinfo` (GCC 16, glibc), web = `build/web-cmake`
+ `wasm-opt -O2` as `tools/web.sh` does, in `tools/web_check.mjs` (headless Chromium 154 on the GPU, `DUMP_FILES`;
a copy of the script next to a `build/web` symlink to serve another build while one ran).

| run | what | result |
| --- | --- | --- |
| base | desktop before N91 | 135000 frames, match over at 110988 |
| pbm | desktop, bundled musl math only | = base, all 135000 frames (heap= too) |
| fix | desktop, + m415/OSFastCast casts | = base, 135000 frames |
| fix2 | desktop, + hsfdraw cast, m418 calls | = base, 135000 frames |
| web1 | web, bundled math, no cast fix (stopped at 1500 s) | splits from desktop at 10869, plr3, in m415: as N87 |
| web2 | web, + m415/OSFastCast casts | = desktop for all 82785 frames it ran (turns 1-8, 11 minigames), then m418's trap |
| web3 | web, + m418 calls | WEB3 |
| fco | desktop with float-cast-overflow, before | 4 sites (above) |
| fco3 | the same after the fixes (`tools/netplay/float_cast_check.sh`) | none, 135000 frames, match over at 110988, = fix |

- **Android arm64** (`cmake --preset android-arm64`, NDK 28 clang, dol + m415Dll + w01Dll built): no `fmadd`/`fmsub`/
  `fmla` in any game object, the math copy, m415Dll.so or w01Dll.so, and none of them imports `sin`/`cosf`/... (they
  import `pb_*`). aurora's mtx has three `fmsub`s, all from the explicit `fmaf()` in `ppc_rsqrte` (ppc_math.h):
  `fmaf` is correctly rounded by the standard, the same bits as x86's and wasm's software `fmaf`.
- Desktop: `nm -D libdol.so` exports the 17 `pb_*`; no game object or REL imports the system's sin/cos/...; libdol's
  remaining glibc `sin`/`cos` imports come from the C++ (aurora, imgui, RmlUi), not game state.
- `tools/check.sh` passes.

## Tried and dropped

- `-include pb_math.h` (forced first, with `<math.h>` included inside it): netplay_match.c defines `_GNU_SOURCE`
  before its includes; a forced `<math.h>` came first and pulled in `<features.h>` without it (`Dl_info` unknown).
- The same header without `<math.h>` (macros before the system header): glibc's `bits/mathcalls.h` pastes the
  function name into `__DECL_SIMD_##name` after expanding it, `__DECL_SIMD_pb_sin` is no macro, syntax errors.
- The decomp's own MSL fdlibm (src/MSL_C.PPCEABI.bare.H, the GC's libm) instead of musl: big-endian word access
  (`__HI(x)` = `*(int*)&x`), no float functions; musl is the same fdlibm lineage, portable, and is already what the web
  build runs.

- A clang desktop build with `-fsanitize=function` (UBSan's "call through a pointer of another function type", the
  check wasm does) to list every such call in one party instead of one browser run per trap: clang 23's x86_64 build
  spun in `HuPrcCall` (process.c:382) at boot, before the first frame, also a problem of its own (the game is only
  built with GCC on Linux; the web's clang build doesn't spin). Not chased; the two hook mismatches were found by
  the script above instead.

## Not verified

- **Android vs desktop hash logs**: no run on a device or emulator. The only emulator here is x86_64 (AVD hst34),
  which would not show arm64's FMA or `fcvtzu` behaviour anyway, and the app takes no `--script`. Checked statically
  instead (above): no contracted FMA, game math through `pb_*`. A float -> u32 of a negative value would still
  differ on arm64 (saturates to 0, x86 wraps); none was hit in this party (UBSan reports every such conversion, not
  only u8), others may exist in boards/minigames this party doesn't reach: follow-up **N101**.
- **iOS, macOS, Windows (MSVC, MinGW)**: not built (no SDKs/toolchains here). iOS/macOS use clang and the same
  `#include_next` wrapper as Android; MSVC takes the `/FI` path in `cmake/pbmath.cmake` and the new `pb_*` lines in
  `dol.def` (the RELs import from dol through it), never compiled.
- **Other boards and minigames**: only w01 and this party's 13 minigames ran (desktop and web). N101 runs
  `float_cast_check.sh` through the rest.
- **A real two-peer browser + desktop match**: not run (the desktop and the browser lobby use different transports,
  iroh vs WebRTC: N96). This task shows the two builds compute the same frames from the same pads.
- **Which `GWPlayer[3]` field moved first at 10869**: not traced field by field (the hash log has per-player
  hashes only); the cause was found from the conversion instead and checked by the fixed run.
