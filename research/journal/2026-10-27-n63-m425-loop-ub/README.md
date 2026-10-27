# N63: m425 (Thwomp) crash in fn_1_E914: GCC removed a loop's exit test

## Cause

`M425DllUnkStruct4` (src/REL/m425Dll/thwomp.c) declares `s32 unk_3C[5]; s32 unk_50;`, but every 6-slot loop
(init :303, fn_1_E210 :1748/:1755, fn_1_E914 :1920/:1945) indexes `unk_3C[0..5]`. On the GC, `unk_3C[5]` is just
`unk_50` (fn_1_E914 sets `unk_50 = unk_34` and then treats it as slot 5). In C, index 5 is undefined behaviour.
GCC 16.2.1 at -O2 concludes the index never reaches 5, so the `var_r29 < 6` test of the :1945 loop can never be false,
and it deletes it. The built object had `add $1,%r13; jmp 80b0` and no compare at all, so the loop never ended. It
writes -1/0/0 over the struct and then the whole heap after it, until a store (line 1949, `unk_6C[i] = 0`) hits an
unmapped page.

This is not a rare race. **Every m425 start crashed on its first draw** (`fn_1_101C4` -> `fn_1_E914`), which is why
N42 saw it "2 frames into the minigame". GCC printed `-Waggressive-loop-optimizations` "iteration 5 invokes
undefined behavior" for :303/:1748/:1755, but **not** for the :1945 loop that it actually broke.

Core of the unfixed run (gdb): `r13` (var_r29) = 27,765,769; the struct read as all -1 / NaN, its pointers
0xffffffffffffffff, and the same stack as N42: `fn_1_E914 :1949 <- fn_1_101C4 :2091 <- Hu3DDrawPost <- Hu3DExec`.
So the guess in the brief (a bad `var_r31` from the draw object's data) was wrong. `hookData` is fine; the index runs away.

## Fix

Under `TARGET_PC`, the two fields become a union: `s32 unk_3C[6]` overlaid on `{ s32 unk_3C_pad[5]; s32 unk_50; }`.
Offsets and size are unchanged and both names still work, so the code keeps the GC's aliasing, now as defined
behaviour. The GC build keeps the decomp layout. No other lines changed.

After the fix, compiling thwomp.c with `-Wall -Waggressive-loop-optimizations` gives no "undefined behavior" or
array-bounds warning. The :1945 loop's back edge is `add $1,%r14; cmp $0x6,%r14; jne`.

## How it was checked (GMPE01, own user dir build/n63run, vsync off)

- `boot-hook.diff` (temporary, reverted): with `PB_N63_TMP` and `backend.skipBootSequence`, bootDll sets 4 COM
  players (characters 0-3, **group i/2**: m425 is 2-vs-2 and indexes its two platforms by group) and calls
  `omOvlCallEx(DLL_m425dll)` straight away (N54's way).
- Unfixed: SIGSEGV right after `m425Dll.so prolog end` with the stack above (core via `coredumpctl dump`).
- Fixed: m425 plays to its end with 4 COMs (`objman>Ovl Return 1`, `OvlKill 1`, back to bootDll), no crash.
- My first fixed run crashed in `fn_1_109EC` (`hookData` NULL, `arg0 = 2`). My own hook caused it: I had set
  `group = i`, and m425 uses group as the platform index (0/1). With `group = i / 2` the run was clean.
- `Error: data none on ARAM 5f0000` shows up in the unfixed and fixed logs alike, so it is not from this change.

## Same bug class elsewhere (filed, not fixed)

`ub-loops-gcc16.txt`: compiling every src/REL and src/game C file with `-Waggressive-loop-optimizations` gives
34 loops in 18 files where GCC says "iteration N invokes undefined behavior". Since m425's broken loop did
not warn, the list is a lower bound. Notable: `src/game/board/shop.c:390` (`DestroyShop`, the N56/N62 crash
site) and :1199 loop over `shopMdlIdx[0][i]` with `static s16 shopMdlIdx[2][1]`, so i = 1 is out of bounds; also
`board/last5.c:1310`, `mstory3Dll/result.c`, `m442/m453 score.c`, `m446 cursor.c`. Filed as N73.

## Not verified

- **The original N42 path** (`--continue` of the turn-7 `--match` save, turn 8). That save is gone (N42's run dir
  was cleaned). The direct jump hits the same function, stack and line, and the loop crashes on any entry, so the
  path to the minigame does not matter.
- **Gameplay fidelity of the platform deformation**: nothing was compared to a GC/Dolphin run. No screenshots were
  taken; only "it plays to the end without crashing" was checked. The union keeps the GC memory layout, so the
  same aliasing happens as on the GC.
- **Human players / online**: only 4 COMs, in one process. No two-peer m425 run.
- **Other compilers** (clang for Android/iOS, MSVC): not built here (no Apple/MSVC toolchains). The fix is plain
  C11 (anonymous struct in a union), which they all support.
- The other loops in `ub-loops-gcc16.txt`: not checked for whether GCC actually miscompiled them.
