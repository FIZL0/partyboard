# N82: the 31 non-loop out-of-bounds accesses GCC 16 flags (N73's array-bounds-gcc16.txt)

## What was done

I reran the scan (`warn.py`: every src/game + src/REL C file with the build's own flags plus `-Warray-bounds=2
-Wstringop-overflow`). It gives N73's 31 lines plus 3 more inside `extern/aurora/include/dolphin/mtx.h`. Those 3 are
MTX macros expanded in src files, the same `&M[0]` pattern as below. I checked each site against the GC code (N69's
`ciso_extract.py` + `ppcdis.py` on GMPE01 Rev 1, offsets from `config/GMPE01_00/rels/*/symbols.txt`) and against the
PC layout (`nm -S` of the built .so files). Then I fixed the 9 sites (19 warning lines) that are real or undefined
behaviour. Each fix sits under `#ifdef TARGET_PC`, with the GC code kept in `#else`.

After: 20 warnings (`array-bounds-after.txt`). All are false positives (below).

## Fixed

| Site | GC behaviour (disassembly / GC symbols) | PC before | Fix |
|---|---|---|---|
| board/mg_setup.c:178 ExecMGSetup | `player_curr` was just set to -1, so it reads `GWPlayer[-1].space_curr`: `lha 12(GWPlayer-48)` = 0x8018FC14 = **`GWPlayerCfg[0].diff`** (GWPlayerCfg 0x28 bytes right before GWPlayer) | same address = **`GWPlayerCfg[2].group`** (libdol: GWPlayerCfg at -0x40, 0x18 bytes of padding) | pass `GWPlayerCfg[0].diff`. **A real PC/GC difference** |
| m424 ball.c `M424DllBallStruct` | 0xF0 bytes; allocated with `fn_1_6078(0xF0)` | two `void *` (`unk64`, `unkA8`, only ever memset) made it 0x100. `unkE8`/`unkEC` at 0xF4/0xF8 sat **past the 0xF0 request**. They landed in HuMem's 32-byte rounding slack, so no neighbour was hit, but it is a real overrun with `SKIP_HU_ALLOC`/ASan | `u32` on PC (GC layout) + `_Static_assert(sizeof == 0xF0)` |
| m456 main.c:399 fn_1_D2C | `lbl_1_bss_70[4]` = `lbl_1_bss_80` (the state). It is set to -1, then the caller sets it to 5 at once | same neighbour today, by link order only | write `lbl_1_bss_80 = -1` |
| m459 main.c:1339 fn_1_45EC | waits on `lbl_1_bss_CC[5].unk_04`: `lwz 4(CC+5*272)` = the bits of `lbl_1_bss_61C.unk_00.y` == 13, a float that is never that denormal, so no wait | entries are 0x138 on PC: it read padding (0), so no wait | compare the bits of `lbl_1_bss_61C.unk_00.y` (exact GC read) |
| m459 main.c:1691 fn_1_535C | `s16 lbl_1_bss_48[1]`, `[1]` = a light id. On GC that is padding before bss_4C (0x4A) | padding today (0xc2d2, next global at 0xc2e0) | `[2]` (as N69's m443) |
| m462 main.c:1376 fn_1_69C0 | `unk16[4]` = `unk1E`, the stop flag fn_1_7DA0 sets | same offset (all-s16 prefix): UB only | read `unk1E` |
| w02 shuffleboard.c:562 fn_1_B8A8 | after the loop `temp_r31` is 15 (`mulli 4,31,12`, confirmed). `lbl_1_bss_298[15].z` = the bits of `lbl_1_bss_34C[4]:[5]`, two board model ids (1..BOARD_MODEL_MAX), a tiny positive float | padding (0.0f) | build the float from `lbl_1_bss_34C[4]`/`[5]` (exact GC read) |
| m449 main.c:1340/1453/1527/1899 | `(&lbl_1_bss_54)[1]` = `lbl_1_bss_58` (0x54, 0x58) | same today, by link order only | one `omObjData *lbl_1_bss_54_58[2]`; `lbl_1_bss_54`/`_58` are macros for its slots |
| m447 player.c:766/767/777/778 fn_1_671C | `[unk00][12]`, `[14]` of `[4][6]` = `[unk00 + 2][0]`, `[2]`, in the table (unk00 is 0 or 1, 2 players) | same address: UB only | index `[unk00 + 2][0/2]` |

The mg_setup site is the only one where PC already did something the GC does not do. With player 0's COM
difficulty d in 1..3, the GC snaps any players standing on space d to that space's corners when the minigame setup
starts (`BoardPlayerMoveAwayStart(-1, d, 1)`). PC used player 2's group instead. Space 0 is invalid
(`BoardSpaceGet` returns NULL), so a 0 does nothing on both.

## Not fixed: false positives

- **Cross-member memsets inside the allocation** (6): m403 main.c:716 (`&unk5C`, 0x40 bytes = unk5C..unk68 exactly,
  no pointers, the same layout on PC, `sizeof` malloc); m415 main.c:777 (`&unk60` + 0x40 = 0x60..0xA0 of a 0xA0 block
  on both, struct 0x78 GC / 0x80 PC); m424 ball.c x4 (now GC layout, 0x64..0xA4 and 0xA8..0xE8 of the 0xF0 block).
  memset uses the whole object's size (`__builtin_object_size` type 0), so `_FORTIFY_SOURCE` does not trap these.
- **`&M[0]` passed where an `Mtx`/`ROMtx` is expected** (14): m418:2027, m425 thwomp.c:2106 and :2715 (`ROMtx`),
  m426:3101, m428 player.c:3121/3162/3351/3353, m430 player.c:821/1015/1122/1129/1137, m442:1742, m450:6128,
  EnvelopeExec.c:238/266/321. In every one `M` is a whole local `Mtx` (or `ROMtx`), so `&M[0]` is the same pointer as
  `M`, and walking its 3 rows is legal C. GCC narrows the region to the first row (16 or 12 bytes). Not one of these
  passes a `Vec` or a single row.

## Checks (GMPE01 Rev 1, own user dirs under build/n82run)

- **Each changed minigame directly** (N73's `boot-hook.diff`, renamed `PB_N82_OVL`, temporary, reverted). The baseline
  build (the changed files reverted to HEAD~ and rebuilt) and this branch were copied to their own dirs (RUNPATH
  `$ORIGIN` first). `--script` with only `end 30000`, `--turbo`, `--hash-log`. m424 (32), m447 (55), m449 (57), m456
  (62), m459 (65), m462 (68): no crash, and hashdiff gives **identical, 30000 frames** for each (game state and `heap=`;
  m424's struct shrink keeps the same HuMem block size). All but m459 reach `objman>Ovl Return`.
- **Which fixed lines ran** (`gdbcov.sh`: gdb hit counts from a command file, new build). m447 fn_1_671C case 1: 113
  hits, case 2: 54. m449 `(&lbl_1_bss_54)[i]`: 7192. m462's loop check: hit. m459 fn_1_535C (`lbl_1_bss_48[1]`): hit.
  m456's `var_r28 == 0` line: **0** (someone scored). m459 fn_1_45EC: **0** in 100000 frames (it is a long rounds
  game and the COMs never finished it).
- **Whole party** (`party.script --match 0@1500 --turbo`, 135000 frames; mg_setup is in libdol): base twice, new twice.
  new-1 = new-2 = base-2, **identical for 135000 frames**, `match over` at 116948. base-1 split from base-2 at 78195
  in m440 (ovl 48): that is N83's known divergence, on the baseline itself. gdb in a new-build party:
  `BoardPlayerMoveAwayStartCurr` at the minigame setups passes `diff` = 1 (`GWPlayerCfg[2].group` = 0, which the old
  build passed). No player stood on space 1 at those points (e.g. spaces 59 44 53 53), so the move-away did nothing
  on either build, which is why the hashes match.
- `tools/check.sh` passes.

## Tried / failed

- `gdb -batch -ex "break X" -ex "commands ..."`: the multi-line `commands` does not attach through `-ex`, so gdb
  stopped at the first hit and `-batch` then killed the game. `gdbcov.sh` writes a `-x` command file instead.

## Not verified

- **w02 shuffleboard (fn_1_B8A8) at runtime**: board w02's shuffleboard event was never played (the party script
  plays w01, and the direct-jump hook only starts minigames). The fix is from the disassembly and the symbol map.
- **m456's no-winner branch and m459's ending wait (fn_1_45EC)**: not reached in the direct runs (above). Both read
  or write what the GC does, from the disassembly. m459's loop does not wait on either build unless that float's bits
  are 13.
- **mg_setup with a player on space `diff`**: no run had a player on space 1..3 at a minigame setup, so the
  corner snap the GC does there (and PC now does) was not seen on screen.
- **Gameplay vs a real GC/Dolphin**: no comparison. Only this port before/after.
- **Two-peer / online runs**: none. Both peers get the same build, and nothing here reads per-peer state.
- **Clang (Android/iOS), MSVC**: not built (no Apple/MSVC toolchains here, and the Android NDK build was not run). The
  fixes are plain C11 (`_Static_assert`, unions for the float bits).
- **The remaining 20 warnings at runtime**: classified from the source and the layouts only (above).
