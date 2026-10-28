# N107: audit of wasm function-type traps (calls through mistyped function pointers)

Brief (from N91): a call through a function pointer whose wasm type isn't the callee's (argument count, return
type, float vs int; pointer types don't matter, they are all i32) runs natively and traps in the browser
(`RuntimeError: function signature mismatch`). Audit every remaining one in `src/` systematically, fix each with
the callee's real signature, and run web parties on w02-w06 without a trap.

N103 (merged before this task) had already fixed the ones N91 listed plus a sweep of explicit casts
(`-Wcast-function-type-strict`) and a by-hand list of `(void *)fn_…` casts. This task covers what that sweep could
not see, with one tool, and runs the boards.

## The audit: fnflow.py

`fnflow.py` (here) reads clang's JSON AST of every C file in `src/` as the web build compiles it (commands from
`ninja -C build/web-cmake -t compdb`, emcc with `-fsyntax-only -Xclang -ast-dump=json`; ~100 s for the 676 files)
and computes wasm signatures from the C types (pointers, ints, enums, structs by value: i32; `long long`: i64;
float/double; a struct result: an sret i32 first parameter; `...`: one more i32). It reports every place a
function's address leaves its own type:

- `MISMATCH`: a function name converted (explicitly, implicitly, or through `(void *)` straight into a typed slot)
  to a function pointer type with another wasm signature;
- `ESCAPE`: a function name converted to `void *` or an integer, with where it goes (callee and argument, field);
- `SINK`: an untyped value (`void *`, integer) becoming a function pointer, with where (NULLs left out);
- `PCAST`: a function pointer *variable* cast to a pointer type of another signature;
- `UNPROTO`: a function stored in an unprototyped `T (*)()` slot; `NOPROTO`: a call with arguments through one;
- `XDECL`: a function taken by address in one file with a declaration whose wasm type isn't its definition's in
  another file (a wrong decomp prototype in a header). wasm-ld does not check these: it warns only for direct calls
  (checked with a two-file test: `void h(int)` declared, `void h(void)` defined, address taken, `emcc -r`: silent).

Its output for the tree before the fixes is `flow.txt` (NULL sinks dropped). Every line was checked:

| report | what it is | verdict |
|---|---|---|
| ESCAPE w10Dll `TutorialHookExec` `int (s16, s32)` -> `BoardTutorialHookSet(void *)`, SINK tutorial.c:59 `void (*)(s16, s32)` | the tutorial board's hook returns `int` (the decomp's, no return value at the end), `BoardTutorialHookExec` calls it as `void` | **trap**, fixed |
| ESCAPE m410 `fn_1_A828` -> `fn_1_77E8(void *)`, SINK game.c:1191 | `void (u16, u16, u16)` both sides | fine |
| ESCAPE mpexDll `fn_1_438` -> `.unk_04` (`void *`) | stored, never called | fine |
| ESCAPE modeltestDll `fn_1_F9C`, XDECL `fn_1_F9C` | declared through a function typedef (`omObjFunc2 fn_1_F9C;`), the tool doesn't resolve that form | false positive |
| ESCAPE web_clipboard.c / web_rel.c `__em_js_ref_*` | Emscripten's EM_JS bookkeeping | not calls |
| SINK hsfdraw.c:2310, hsfanim.c:521 | `HU3DMODEL.hsf`/`hookFunc` union; every `Hu3DHookFuncCreate` caller is typed (N91 checked all 108) and fnflow sees no MISMATCH among them | fine |
| SINK objmain.c:264, m423 main.c:5292 (`unk4C`) | only ever NULL | fine |
| SINK netplay_match.c:674 | port code, a return address | not a call |
| MISMATCH / XDECL `void ()` / `s32 ()` sources (mushroom.c, m414, w02 main.c, modeltest) | K&R `()` declarations whose definitions take `(void)` (checked each): the same wasm type | false positive (the tool reads the unprototyped declaration) |
| UNPROTO (44): `BoardWinComKeyFuncSet`, pre/post-turn hooks, m420's table, m450 `fn_1_6CB0` | `(void)` functions with the slot's return type; NOPROTO finds no call with arguments through any `()` pointer | fine |
| PCAST m420 map.c:438/1095, m450 main.c:1921 | `void (*)()` -> `void (*)(void)`, holding the `(void)` functions above | fine |
| PCAST web_rel.c:51/52 | REL mark functions, only their addresses are read (N103) | fine |
| NOPROTO last5.c `HuPrcVSleep(8)` | a direct call with an argument to the unprototyped `void HuPrcVSleep()` (defined `(void)`): clang's WebAssemblyFixFunctionBitcasts drops the extra argument, the link reports nothing | fine |

Direct calls across files with mismatched declarations: wasm-ld warns `function signature mismatch` for those (and
links a trapping stub). Re-running all 91 REL prelinks (`emcc -r`, the commands from `ninja -t commands`) and the
final link printed no such warning.

So the static audit found one more trap, `TutorialHookExec`, confirmed in the old binary: `wasm-opt
--extract-function` of `w10Dll.TutorialHookExec` is `(param i32 i32) (result i32)`, and the `call_indirect` in
`BoardTutorialHookExec` is `(type (func (param i32 i32)))`. It would trap on the tutorial's first hook call.

**Fix:** on `TARGET_PC`, `TutorialHookExec` is `void` (include/REL/w10Dll.h, src/REL/w10Dll/tutorial.c; the GC build
keeps `int`). Nobody reads its result.

## The board runs

`runweb.sh` (here): `tools/web_check.mjs`, headless Chromium on the GPU, `STOP_ON=RuntimeError`,
`tools/netplay/party.script --match B@1500 --turbo --hash-log /tmp/hash.log`, GMPE01 Rev 1, fresh profile per run,
five boards in parallel (~70 fps each). "HEAD" is the web build of 2513487f (N103's fixes in, none of this task's);
"fixed" is this branch's code (the w10 fix, no shortcut) **plus N105's candidate viewport patch** (not committed,
see m411 below). Desktop runs: the same script and flags on the desktop build of the same code (the candidate
patch too; render state only), for comparison.

| board | build | result |
|---|---|---|
| w02 (`--match 1`) | HEAD | **whole party, no trap**: match over at 112467, the results (mstory3), script end at 135000 |
| w03 (`--match 2`) | HEAD | no trap for 6 turns, then **m444 never ends** (frame 56858, turn 6, overlay 52): the game runs but the hash cycles through 4 values (period 14) for ever. The desktop does the same: its hash log equals the browser's for frames 55000-59999 (`hashdiff.py`) and loops the same way. Not a function type: follow-up **N108** |
| w03, seed 0x50424E55 | fixed | no trap for turns 1-8 (to frame 67636), then m444 again (entered mid-turn, point 2 after player 1: a board event) and the same hang |
| w04 (`--match 3`) | HEAD | no function trap; at frame 70895 (turn 8) m411's first frame **aborted on a WebGPU validation error**: `SetViewport(1284, 20, -20, 938)` "contains a negative value" (below) |
| w04 | fixed | **whole party, no trap**, m411 played (70895-73818); match over at 93247; hash log **equal to the desktop's for all 86897 frames** pulled live (heap= aside), and the same match-over frame and final hash at 135000 (`8C40702BD3B5D2B2`) |
| w05 (`--match 4`) | HEAD | **whole party, no trap**: match over at 125067, script end at 135000 |
| w06 (`--match 5`) | HEAD | **whole party, no trap**: match over at 97087, script end at 135000 |

w02, w05 and w06 ran on HEAD. This task's code changes only w10Dll, which those runs don't load, so they stand for the
final build too.

**Browser vs desktop splits (not traps):** the desktop's final hashes differ from the browser's on w02, w05 and w06.
From the reseed lines: w02 equal to the reseed at 16899 (turn 2, player 2), then the next turn starts at 20091 on
the desktop and 21254 in the browser (match over 120027 vs 112467); w05 equal to 29641 (turn 3), different from
30114 on (match over 113527 vs 125067); w06 equal to 48229 (turn 6, player 0), then the desktop goes into m444 at
49656 and hangs there (N108) while the browser plays on. Most likely float -> int conversions like N91's m415 (N106
sweeps those); the frames are in follow-up **N110** (w05's were found after it was filed: they are only here). w03 (to 59999) and w04 (all 86897 compared) agree.

The pages stop answering CDP after the script's end (`OSResetSystem is a stub`), so web_check's `DUMP_FILES` gets
nothing and the hash logs of the finished runs were lost; `cdp_file.mjs` (here) copies a file out of a live page
in 512 KB chunks (larger CDP replies never arrived), which is how w04's and w03's logs were compared.
`cdp_pause.mjs` pauses a running page and prints its stacks (how the m444 hang was told apart from a wasm spin:
the page kept yielding and its frames advanced), `cdp_eval.mjs` evaluates one expression.

### m411: a negative viewport

m411's split screen (`fn_1_8A80(arg0, arg1)`, main.c:2322) sets camera 4's viewport to `(arg0 + 2, 10, 630 - arg0, ...)`; at
`arg0 = 640` (one player's view at full size) that is x 642, width -10 (x2 on the 1280-wide target). GX takes a
negative width (a mirrored viewport; here all of it is outside camera 4's scissor, which aurora clamps to nothing).
WebGPU rejects it, aurora logs FATAL and the page aborts. Desktop release builds run Dawn with `skip_validation`
(extern/aurora/lib/webgpu/gpu.cpp), so the desktop passed it to Vulkan and never reported it.

This is task **N105** (filed by N97, open, queued before this one), so it is **not fixed in this branch**: this
task does N107 only, and a second fix landing next to N105's would clash. For the w04 run below the build had a
candidate fix, kept here for N105: `n105-candidate-0010-gfx-negative-viewport.patch` (a
cmake/patches/aurora patch): aurora's render pass gives WebGPU the same span with a positive size (`left += width;
width = -width`, the same for height), not mirrored; the only case seen is fully scissored away, render state only.
It applies in order after 0001-0009 on a clean aurora checkout (checked). N105 still has to decide between this and
skipping the camera, and grep the other RELs for negative sizes.

## Tutorial (w10) check

The tutorial isn't reachable from a `--match` party. `tutorial-hook.diff` (here, **not committed**) is a bootDll
shortcut that starts w10 as selmenuDll does (`_SetFlag(0x1000B)`, `omOvlCallEx(DLL_w10dll)`, all COM), with
`PB_N107_TUT=1` (`?env=PB_N107_TUT=1` in the browser) and a script tapping A on every port every 20 frames.

- Desktop: 11 `Tutorial Hook ...` calls (SCEN 0..7), then a SIGSEGV in `last5.c` `InitHost` ->
  `BoardModelMotionCreate` -> `LoadHSF` -> `HuMemMemoryAlloc2` (coredumpctl).
- Browser (fixed build + shortcut): the same 11 hook calls **without a signature trap**, then `memory access out of
  bounds` at the same point. Hash logs equal for all 1197 frames.
- The crash comes after the hooks, in the last-5-turns event, on both builds; whether the real tutorial (from the
  menu) gets there is not known: follow-up **N109**.

## Tried and dropped

- clang `-fsanitize=function` on a desktop build (the brief's alternative): not tried. N91 found clang 23's x86_64
  build spins in `HuPrcCall` at boot, and a clang desktop build means building aurora/Dawn again under load (load
  average 65 here). The AST audit plus the browser itself (which checks every indirect call) covered the same ground.
- `-Wpedantic` for the implicit `void *` <-> function conversions: it reports those but not explicit `(void *)fn`
  casts, and gives no destination for a `void *` value; fnflow.py replaced it.

## Not verified

- **w03 past turn 8**: both seeds end in m444's hang (N108), desktop and browser alike. Turns 1-8 ran with no trap.
- **The tutorial past its 11th hook call**: it crashes in last5 on both builds under the shortcut (N109). The fix
  is shown by 11 calls in the browser without the trap; the old trap by the binary's types, not by a run of the old
  build with the shortcut (the old build had no shortcut, and rebuilding it was not worth it after the binary
  check).
- **Overlays no party reaches** (story mode, mgmode menus, w20/w21, the other minigames): covered only by the static
  audit, no browser run. The audit cannot see a function pointer stored as bytes (memcpy, a union read through
  another member beyond the two unions checked), C++ files (port code, not decomp), or code compiled out of the
  web build (`#ifndef TARGET_PC`).
- **w04 without the candidate viewport patch**: the committed code still aborts in m411 in the browser until N105
  lands; w04's full party ran with the patch applied. No function-type trap on w04 either way (HEAD ran to turn 8,
  m411's abort is a WebGPU validation error).
- **clang `-fsanitize=function` desktop run**: not tried (above).
- **Android/iOS**: function types don't trap natively; nothing to run there for this task.
