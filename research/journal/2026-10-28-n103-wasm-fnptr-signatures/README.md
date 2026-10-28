# N103: wasm `function signature mismatch` traps from decomp function-pointer types

Brief: m418 (overlay 26) trapped in both tabs of N99's run rl1 about a second in (`RuntimeError: function signature
mismatch` in `m418Dll.fn_1_9BA4`). Its sequence tables hold `void fn(s32)` / `s32 fn(s32)` functions, and
`fn_1_5D58`/`fn_1_84B0`/`fn_1_9364` called them through `void(void)` / `s32(void)` pointers. x86 and the GameCube
don't care. Wasm's `call_indirect` checks the callee's exact type. Then sweep the other RELs for the same thing.

## m418

`M418DllUnkStruct8` (the table rows) already had the right types (`M418DllInFunc`/`M418DllInRetFunc`). The cast was in
`fn_1_AE8C` (sequence.c), which copied a row's functions into `M418DllUnkStruct7`'s `unk10`/`unk14`
(`M418DllFunc`/`M418DllRetFunc`). Fix: struct7's slots are now `M418DllInFunc`/`M418DllInRetFunc`, the casts are gone,
and the three callers pass 0. No step function reads its argument (all 36 checked), so 0 is as good as GC's leftover
r3.

## The sweep

The tool: `-Wcast-function-type-strict` on every C file of the web build. I took the commands from
`ninja -C build/web-cmake -t compdb`, swapped `-o` for `-fsyntax-only` and ran emcc on each (`castscan.py` here).
Only casts whose **wasm** signature differs can trap. Pointer ↔ pointer and pointer ↔ s32 parameters are all i32 on
wasm32, so the ~40 `void(omObjData *, StructX *)` → `void(omObjData *, void *)` hits (mentDll, mpexDll, ztardll,
mstoryDll) are harmless. The strict warning can't see casts through `void *`, so I also grepped `(void *)fn_…` /
`(void *)Name` and checked each target against the slot's type and its call site (`voidcasts.py`). Also checked: the
unprototyped `s32 (*)()` hook slots (pre/post-turn hooks), `BoardWinComKeyFunc`, m420/m450's `void (*)()`.

Fixed (every one would trap on the web when reached; none of their arguments or return values is used):

| where | was | now |
|---|---|---|
| m418Dll sequence step functions | `void(s32)`/`s32(s32)` called as `(void)` | struct7 typed `In`, callers pass 0 |
| w03Dll statue.c `fn_1_2930` (pre-turn hook) | `s32(s32)` called as `s32()` | `s32(void)` |
| w03Dll main.c `fn_1_AF8` (land event, `s32(void)` slot) | `void(void)` | `s32(void)`, returns 0 (the caller drops the result) |
| w03Dll statue.c `fn_1_2B44` (`Hu3DHookFuncCreate`) | `void(void)` called as `(model, mtx)` | `(HU3DMODEL *, Mtx)` |
| board/boo.c `BallRenderHook` (`Hu3DHookFuncCreate`, any board's Boo) | same | same |
| w02Dll `fn_1_94AC`, `fn_1_BE74` (`HuPrcDestructorSet2`) | `s32(void)` with no return | `void(void)` |
| w05Dll mg_coin.c `fn_1_9B74` (post-turn hook, `s32()` slot) | `void(void)` | `s32(void)` returning `BoardModelAttrReset(...)`: the desktop build tail-called it (objdump: `jmp BoardModelAttrReset`) and GC left its result in r3, so the hook's "done" value is the same as before |
| mpexDll charsel.c `fn_1_157C4`, `fn_1_15D48` (slot `void(omObjData *, void *)`) | `void(void)` | `(omObjData *, void *)` |
| mentDll `fn_1_81A8` (same slot type) | 3 parameters | 2 |
| mgmodedll `fn_1_19F5C`, `fn_1_1F32C` (same) | 1 parameter | 2 |
| mstory3Dll result_seq.c (17 functions) and result.c `fn_1_18CFC` (`StructBss1530.unk04`, called `(obj, NULL)` on PC) | `void(omObjData *)` or `void(void)` | `(omObjData *, void *)`; the one direct call (`fn_1_D7D8`) passes NULL |
| m435Dll `fn_1_B1F4`, m436Dll `fn_1_62C4` (an object's `func`, `void(omObjData *)`) | `s32(omObjData *)` ("must return s32 to match") | on TARGET_PC the object gets a `void` wrapper (`fn_1_B1F4_obj`/`fn_1_62C4_obj`) that calls it, so the matching body stays |

Left as they are: `web_rel.c`'s mark functions cast to `void(*)(void)` (only their addresses are read), and every
pointer-for-pointer parameter cast. After the fixes the strict scan reports nothing else in `src/`.

## Checks

The boot hook: `research/journal/2026-10-27-n83-m440-determinism/harness.diff`'s bootDll hunk, applied for the runs
and **not committed**. For the web it also needed `|| getenv("PB_N73_OVL")` in the skipBootSequence condition: the web
build has no config.json switch for it. The page sets env vars with `?env=PB_N73_OVL=26`. The script is
`seed 0x50424E54 / end 6000 / mg-explain off` (`m.script`), all four players COM, `--turbo --hash-log`.

- **Desktop, m418 unchanged.** objdump of old vs new m418Dll.so differs only in an `xor %edi,%edi` before each of the
  six calls (plus nop padding). Two runs each with the old and the new .so: all four hash logs are byte-identical,
  every field including heap. 4137 frames in ovl 26, then back to ovl 1.
- **Desktop, the other RELs** (old vs new .so for each changed target, objdump with addresses stripped): mentDll,
  mgmodedll, mpexDll, w02Dll, w05Dll: no difference. w03Dll: `fn_1_AF8` returns 0. mstory3Dll: one added NULL
  argument. libdol: only lines in `lzma_raw_coder_init`, nothing in boo.c's code. m435/m436: GCC compiles the s32
  bodies differently now that only the wrapper calls them. Two old and two new runs each: identical hash logs, m435
  (overlay 43, 3954 frames to its end) and m436 (44, all 5996 frames).
- **Reached:** gdb hit counts in the new desktop build: m418 `fn_1_57E4` 90, `fn_1_8B14` 90, `fn_1_5768`/`fn_1_6808`/
  `fn_1_888C` 1 each; `fn_1_B1F4_obj` 3952; `fn_1_62C4_obj` 5993.
- **Web** (WEB_FAST build with the fixes and the hook, `tools/web_check.mjs`, one tab each): m418, m435, m436 all
  ran to the script's end at frame 6000, no trap, and each final hash equals the desktop's (m418 `4FCDFD6001369E1D`,
  m435 `10E556C53A1C9A8C`, m436 `2332FD5D4F9F934C`). The full `--hash-log`s (6000 frames each, `DUMP_FILES`) match
  the desktop's in every field but `heap=`, which already differs at frame 0 (the heap layout isn't the same on
  wasm32 and x86-64).
- `tools/check.sh` passed with the fixes, before the hook was applied.

Gotcha: I swapped sources to HEAD (to build the old .so files) while a web build was running. Rerun web.sh after
something like that. The restored files are newer, so ninja rebuilds them.

## Files

- `castscan.py`: the strict cast scan over a compdb (`python3 castscan.py cdb.json`, emsdk env loaded).
- `voidcasts.py`: lists `(void *)fn_…` casts with the call they're in and the target's definition.
- `run1.sh`: one boot-hook minigame run (`run1.sh GAMEDIR OUTDIR OVL`; needs `cfg.json` = skipBootSequence and
  `m.script` next to it).

## Not verified

- **The old web build trapping in m418 under this harness.** I didn't rebuild the old wasm. The trap is N99's
  (rl1), and the gdb counts show these runs reach the fixed calls.
- **web_pair.mjs (two tabs, online) through m418.** I used one tab with the boot hook. Same code path, no lockstep.
- **The other fixes on the web:** mpexDll (character select), mentDll, mgmodedll, mstory3Dll (story mode result),
  w02 destructors, the w03 statue / land event / pre-turn hook, w05's coin minigame hook, the Boo ball render hook.
  Board events and story/menu screens aren't reachable with the boot hook in a reasonable time. The desktop
  disassembly shows only the intended differences.
- **Mismatches the scans can't see:** a function stored through an integer or a struct copy, unprototyped calls
  with the wrong argument count, C++ files. The scans covered casts in C files and `(void *)`/`(u32)` casts of
  function names.
- `build/web` in this worktree was last built with the temporary hook. Nothing is committed from it, and the next
  web.sh rebuilds bootDll.
