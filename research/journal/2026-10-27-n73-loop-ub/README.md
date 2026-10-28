# N73: loops over out-of-bounds decomp arrays (N63's list of 34)

## What was done

1. **All 34 warning sites fixed in the source** (18 files), each under `#ifdef TARGET_PC`, GC code kept in `#else`.
   After the fix, compiling all 324 src/game + src/REL C files with the build's flags gives **no**
   "iteration N invokes undefined behavior" warning (was 34).
2. **Blanket guard**: `CMakeLists.txt` adds `-fno-aggressive-loop-optimizations` (C only) to `dol` and every REL
   target when the compiler accepts it (`check_c_compiler_flag`). GCC then stops using declared array sizes to
   bound loop trip counts. Clang rejects the flag ("unknown argument"), so Android/iOS builds go without it. They
   rely on the source fixes.

Proof that the guard covers what the warnings miss: pre-N63 `thwomp.c` (`git show 30116363^:...`), compiled with the
build's flags. Without the flag, the :1945 loop's back edge is `jmp 80b0` with no compare (N63's crash). With the
flag it is `cmp $0x6,%r14; jne`, and the 3 warnings go away too.

## The sites

Each fix keeps the GC layout (a union, or one array as wide as the fields it covered), except where the GC
layout cannot hold on PC (8-byte pointers) or nothing aliases it.

| Site | What the code does | Fix |
|---|---|---|
| board/shop.c:390, :1199 | `shopMdlIdx[0][0..1]` of `[2][1]` | index `shopMdlIdx[i][0]` (same address) |
| board/last5.c:1310 | `j=3; while(j>=0){j--; ...}` runs with j = -1: `character[-1]`, member 0, `playerOfsTbl[-1]` | stop at j = 0 (see below) |
| E3setupDLL mgselect.c:306/399 | `mgSelect.unk_2E/unk_38[0..4]` of `[4]` | `[5]` (slot 4 was unk36/unk40, unused) |
| E3setupDLL mgselect.c:449 | `mgPic.unk_00[0..13]` of `ANIMDATA *[2]` | `ANIMDATA *unk_00[14]` (**real PC overrun**, below) |
| m415 map.c:634 | clears `unkSubStruct.unk74[0..15]` of `[2]` (GC: the 0x20 bytes to the end) | `s16 unk74[16]`; unk78/unk7C/unk80 were never used |
| m419 main.c:463 | 11 motions in `unk06[8]` (over unk16..unk1A) | union `unk06[11]` |
| m420 player.c:335 | kills `unk_C4[0..10]` of `[10]`, slot 10 = unk_D8 | union `unk_C4[11]` |
| m427 map.c:1419 | `unk18E[0..4]` of `[2]` (over unk194 and padding) | `s16 unk18E[5]` + 2 bytes padding |
| m428 map.c:502 | quads write `unk_0C[3]` of `s32[3]` (= unk_18, unused) | union `unk_0C[4]` |
| m440 main.c:1441/1775 | `unk20[0..8]` of `unkStruct11[3]` (over unk38[0x30]) | `unk20[9]` |
| m442 score.c:58/71/133/147 | 8-sprite loops over `unk_0C[6]` (+unk_18, unk_1A) | union `unk_0C[8]` |
| m453 score.c:65/78/132/145 | 6-sprite loops over `unk_0C[4]` (+unk_14, unk_16); kill loop runs to **7** | union `unk_0C[6]`; 7th kill below |
| m443 main.c:795 | `s16 lbl_1_bss_10[1]` holds 2 sprites (GC: slot 1 = padding before lbl_1_bss_14) | `[2]` (PC wrote the next global) |
| m446 cursor.c (6 loops) | `unkC[0..9]` of `s16[9]` (slot 9 = padding before Vec unk20) | `[10]` |
| m446 stage.c:129 | 3 lights in `unkA[2]` (slot 2 = unkE) | union `unkA[3]` |
| m447 main.c:710 | `for (i = 0; i <= 25; ...) espDispOff(unk34[i])`, unk34 is `[25]` | `i < 25` (slot 25 is uninitialised malloc padding) |
| m449 main.c:1147 | counts up `unk_1C4[0..15]` of `[4]` (over unk1D4[0x30]) | `s32 unk_1C4[16]` |
| mstory3 result.c:1010/1383 | `unk34[0..11][2]` of `[4][2]` (rows 4..11 = unk54..unk90) | union `unk34[12][2]` |
| ztardll main.c:906 | 6 characters in stack `s16 sp14[4]` | `[6]` (**real PC stack overlap**, below) |

### Bugs that were real on PC (not only UB)

- **ztardll fn_1_40E4**: GCC put `spC[4]` right after `sp14[4]` (`sp14` at 0x8(%rsp), `spC` at 0x10). `sp14[4..5]`
  are `spC[0..1]`. The loop that fills sp14 compares against `GWPlayerCfg[spC[0]/spC[1]].character` while it overwrites
  them with character numbers (0..7), so it indexed `GWPlayerCfg` past its 4 entries. The shuffle
  then swapped spC's slots too. The GC had free stack there.
- **E3setupDLL mgPic**: 14 `ANIMDATA *` (13 pictures + cursor) went into a 96-byte struct. On PC they overwrote
  `unk_40` (the sprite group, set just before the loop) and ran 16 bytes past the malloc. On the GC the 14 words
  ended at 0x38 and missed unk_40 at 0x40.
- **m443 lbl_1_bss_10**: the second sprite id went into whatever global the linker placed next.

### Sites where the GC behaviour needed a decision

- **last5.c InitLotteryTicket**: the 4th iteration (j = -1) on the GC writes `character[-1]` = `unk02` (never read),
  creates a sprite and an ANIMDATA and calls `HuSprGrpMemberSet(group, 0, ...)`, which is a no-op because member
  0 is set (sprman.c:422). Only group members are drawn (HuSprBegin), so that sprite is never shown and leaks.
  `HuSprAttrSet(LINEAR)` repeats what member 0 already has. `HuSprPosSet(group, 0, playerOfsTbl[-1])` reads the 8 bytes
  before playerOfsTbl. In GMPE01's symbols (`config/GMPE01_01/symbols.txt`) that is the end of `messBaseTbl`
  (0x8013B214, values 0, 0), so it puts member 0 at (0, 0), where it already is (HuSprCreate default). On PC the
  bytes before playerOfsTbl are some other data. So the PC loop stops at j = 0: nothing visible changes, and the
  leaked sprite goes away. The current GCC build did run all 4 iterations (r12 counted members 3..0), so the loop
  was not miscompiled.
- **m453 fn_1_91D8**: the kill loop runs 7 times over 6 sprites. On the GC the 7th id is the big-endian high half of
  `s32 unk_18` (0 or 1), so it is always 0, and it calls `espKill(0)`. The old PC build read the low half (esp 0 or 1).
  The fix kills the 6 sprites and then `espKill(unk_18 >> 16)`, which is the GC's value.
- **m447 fn_1_1CE0**: slot 25 is `unk66` padding of a `HuMemDirectMallocNum` block that is never written, so the
  value is garbage on both machines. Not reproducible, so the PC loop stops at 24.

### Which loops GCC actually broke

I checked these in the old objects. None of them had lost its exit test:
- shop.c DestroyShop: unrolled into two loads, `.bss+0x2c` and `+0x2e`.
- last5.c: 4 iterations.
- m453 kill loop: 7 iterations.
- ztardll copy: 6 bytes.

The others were not checked one by one. Compiling every file with and without `-fno-aggressive-loop-optimizations`
changes code in ~90 files (`fdiff.py` approach: objdump per function). Most of those changes are legitimate trip-count
uses of in-bounds arrays, so that diff does not identify miscompiles by itself. The guard and the source fixes
together make this moot.

## Checks (GMPE01 Rev 1, own user dirs under build/n73run)

- **Offline mash-a** (`--script tools/netplay/mash-a.script --turbo --hash-log`): baseline (HEAD sources, same build
  dir) and this branch are **byte-identical**, 17400 frames, end `AE36457C2B93C6C0`.
- **Whole party** (`tools/netplay/party_check.sh`, 135000 frames, `--match 0@1500`): baseline twice and new twice, each
  pair identical. Baseline vs new: **identical game state for 135000 frames**. The `heap=` layout column first differs
  at frame 8648 in ovl 23 (m415), whose `unkSubStruct` is now smaller on PC (`unk74[16]` replaces 2 pointers + 0x14).
  Minigames in it, in order: m415, m434, m443, m456, m417, m412, m441, m430 (ovls 23, 42, 51, 62, 25, 20, 49, 38).
  Neither run reached `match over` within 135000 frames (same as baseline).
- **Each changed overlay directly** (`boot-hook.diff`, temporary, reverted: with `PB_N73_OVL=<ovl>` and
  `backend.skipBootSequence`, bootDll sets 4 COMs, groups from mgInfoTbl type (1v3: player 0 alone, 2v2: i/2,
  else i), and calls the overlay; `--script` with only `end 30000`, `--turbo`). Run once with this branch's .so and
  once with the baseline .so swapped in (same libdol):
  - m415, m419, m420, m427, m428, m440, m442, m443, m446, m447, m449, m453 all play to `objman>Ovl Return` with
    no crash on both. hashdiff: identical for 30000 frames, except m415's heap layout (above).
  - ztardll (98), mstory3Dll (78) and E3setupDLL (2) sit in their menus for 30000 frames, no crash, identical.
- Overlay indices from the preprocessed `include/ovl_table.h` (`cpp -DTARGET_PC`). The first try at ztardll used 102
  (I had counted the JP `#if` branch) and segfaulted in `omDLLSearch` on both builds, a bad index rather than a bug.
- `tools/check.sh` passes.

## Tried / failed

- First attempt to diff "with vs without the flag" as a miscompile finder: ~90 files differ, mostly legitimate, so it
  is not a usable per-site test (above).
- gdb breakpoint on ztardll `fn_1_40E4` during the idle ztardll run: never hit (it runs on a later selection), so the
  stack overlap fix was not seen at runtime.

## Leftovers filed

- **N79**: `-Warray-bounds=2` / `-Wstringop-overflow` list 31 more out-of-bounds accesses that are not loop exit
  tests (`array-bounds-gcc16.txt`), e.g. board/mg_setup.c:178 `PlayerState[-1]`, m459, m462, w02 shuffleboard.

## Not verified

- **ztardll fn_1_40E4 and mstory3 fn_1_194A0/fn_1_1C534 at runtime**: the direct jumps sit in the overlays' menus and
  never reach those functions (breakpoint not hit for ztardll). The fixes are layout-identical (mstory3 union) or
  only add stack space (ztardll). The ztardll overlap is from the old object's disassembly.
- **E3setupDLL's picture menu** (`CreateMGPic`/`UpdateMGPic`): the overlay ran for 30000 frames with no crash on
  both builds, but nothing checked that the picture sprites look right. No screenshots.
- **last5 lottery tickets on screen**: the party run covers the board, but I did not check that a lottery ticket
  was drawn in it or compare the ticket's looks with the old build. The reasoning above (member 0 already set, (0, 0)
  position) is from the source and the GC symbol map.
- **Gameplay fidelity vs GC/Dolphin**: no comparison with a real GC run for any site. Only this port before/after.
- **Two-peer / online runs**: none. Both peers run the same build, and the changes are the same on both.
- **Clang (Android/iOS) and MSVC**: not built here (no Apple/MSVC toolchains; Android NDK build not run). Clang does
  not accept `-fno-aggressive-loop-optimizations`, so it gets no blanket guard. The source fixes are plain C11
  (anonymous structs in unions).
- **Performance cost of the flag**: not measured. mash-a's turbo time was about the same (~15 s both).
