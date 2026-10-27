# N69: the GCC-flagged reads (N53 follow-up)

N53 left `gcc-warnings.txt` (143 `-Wmaybe-uninitialized`, 37 loops past their array), two functions that fall
off their end while their caller reads the value, and 20 'memory free error's at a minigame's end. This task read
all of them and fixed the ones whose value can come from stale stack, a register's leftover (on PC: whatever the
compiler left there, which can be an address) or freed/foreign memory, where that reaches game state or memory.
Everything is `TARGET_PC`, so the GC build still matches.

## How the GC values were found

GC's value for a missing return or an unset register is often knowable: the decomp is matched, so the original
code is the decomp's. `ciso_extract.py` (here) pulls `main.dol` and any `*.rel` out of `$PB_DISC`, and
`ppcdis.py` disassembles a function with `llvm-mc -triple=powerpc` (offsets from
`config/GMPE01_00/[rels/<rel>/]symbols.txt`). Disc data stays in the scratch dir; nothing extracted is committed.
`warnctx.py` prints each warning's function with the lines that touch the variable, which is how the list was read.

## Fixed

### The two missing return values

- **w03 `fn_1_12C8`** (statue.c, declared `s32` in main.c, `fn_1_AF8` tests it). It ends with
  `BoardRollDispSet(1)`. That function does not touch r3 when there is no roll display (r3 = the argument, 1);
  with one, its last call is `UpdateRollSprite`, whose last call is `HuSprAttrSet` or `HuSprPosSet` on member 1,
  and neither writes r3: r3 is the roll display's sprite group. New `BoardRollDispSetResult(arg)` in
  board/player.c (`TARGET_PC`) returns exactly that, and `fn_1_12C8` returns it. Before, PC returned whatever
  `UpdateRollSprite`'s last call left in eax.
- **mstory3 `fn_1_16924`** ("TODO PC return value", result.c). START returns `HuAudFXPlay(1)` (r3 after the call,
  nothing after it writes r3), the 300-frame timeout returns 1 (`HuPrcVSleep` is inlined `HuPrcSleep(0)`, which
  resumes from `gcsetjmp` with `gclongjmp(..., 1)`), and A returns the window `fn_1_1834(-1, 1)` makes, which is
  stored in `unk10` and is still in r3 at `blr`. 0 opens the detail pages (`fn_1_16AD4`), non-zero leaves.

### The double kill at a minigame's end

Breakpoint on `memory.c:122` (the error), then a second run (gdb turns off ASLR, so addresses repeat) breaking on
frees of that block: m456's own end, `fn_1_8C68` (stage.c), `HuSprAnimKill`s its four anims. Three of them belong
to particles (`Hu3DParticleCreate` took a use: `useNum` 1 -> 0, freed). `omOvlKill -> Hu3DAllKill ->
Hu3DModelKill` then kills them again for the particle models: five frees each (bank, pat, bmp, raw, anim) of
blocks already freed, 20 errors. It is the same on GC (one block per anim there). Any allocation between the two
would have been freed under its owner.

Fix (general, not only m456): `HuSprAnimKill` (sprman.c, `TARGET_PC`) keeps an anim whose use count reached 0 while
a live particle model still draws with it (`Hu3DParticleAnimUseCheck`, hsfman.c). The particle's own kill frees
it: `Hu3DModelKill` clears the model's `hsf` before it calls `HuSprAnimKill`, so it no longer counts as a user.
Also, the particle no longer draws with a freed anim in between.

### Found on the way

- **`HuSprAnimKill`'s texture loop** (sprman.c, `OPTIMIZED_TEXTURE_LOADING`, PC only) ran `i < anim->bmpNum`.
  `HuSprAnimMake` anims (every window, and more) have `bmpNum = 1|ANIM_BMP_ALLOC` = 0x8001: each kill walked 32769
  `ANIMBMP`s past the block and `GXDestroyTexObj`ed whatever looked initialized, which also writes 0 into it.
  What looks initialized depends on the pointers stored in the neighbouring heap blocks. Now masked with
  `ANIM_BMP_NUM_MASK`.
- **m415 `fn_1_66AC`**: the PC branch points its anim's bitmap at `Hu3DShadowData.buf` (aurora's GXCopyTex keeps
  a reference). The anim's kill then freed the shadow buffer and `Hu3DAllKill` freed it again: the 21st
  'memory free error' (gdb: the block's `retaddr` was `HuSprAnimKill+378`, sprman.c:480). Now the anim's own block
  is freed there and `ANIM_BMP_ALLOC` cleared, so the kill leaves the buffer alone.

### Uninitialized reads that reach game state (or memory)

| where | what | GC's value | PC now |
|---|---|---|---|
| board/com.c `BoardComUseItemSlotGet` | `item` unset when the COM chose no item (`itemUse == -1`): a stray small value made it use an item it holds | `ExecItemPick`'s r31, a data address: never an item, so "no item" | `item = -1` (same as GC) |
| w02 mg_coin.c `fn_1_53B8` | a COM's buttons on frames its timer has not run out | omMain's r29, the objman's heap address: bit 8 decided whether the COM pressed A at once | 0 (presses when its timer runs out) |
| w04 mg_item.c `fn_1_A7E8` | a COM's stick (`var_r23`) and buttons (`var_r17`) for its first 30 frames: could move the cursor or pick an item | the caller's registers | 0 |
| board/item.c `ExecItemBag` | a COM's buttons before its first press | the caller's r25 | 0 |
| w05 dolphin.c `fn_1_5794` | `var_r26`/`var_r24` only set if the ride reached its third stage, then `->work[1] = 1` | stale pointers | NULL, write skipped |
| m416 `fn_1_38F0` | no target cell: `sp20[0].unk2` (stale stack) indexes the grid at column -1, and it is written back | stale stack | return early; every path there ended without moving the COM |
| m419 `fn_1_78B4` | `var_f27` (marked "! - uninitialized") sets a COM's run-off angle `unkC4` | f27 left by `fn_1_9AF0`'s loop (a squared distance at a038) or the creator's at process start | 0 |
| m435 `fn_1_46D8` | an angle of exactly 360.0f (`-tiny + 360` after one wrap) is in no range | the caller's r31 | `sp8[0]` (360 = 0 degrees) |
| m417 `fn_1_17C0` | `MGSeqPosSet(sp8, ...)`, `sp8` "! - uninitialized" | stack slot sp+8 of the frame | -1 (no-op; only moves a sequence's text if 0..7) |

### Loops past their array

| where | what | PC now |
|---|---|---|
| m443 `lbl_1_bss_10[1]` | the setup writes `[1]`: on GC the padding before `lbl_1_bss_14`, on PC whatever the linker put next | `[2]` |
| m447 `fn_1_1CE0` | `i <= 25` over `unk34[25]`: `[25]` is `unk66`, heap bytes nobody set, and `espDispOff` indexes `esprite[]` with it and sets a sprite's attr | `i < 25` |
| ztardll `fn_1_40E4` | `sp14[4]` gets the 6 characters left; GC's `[4]`/`[5]` were stack slack, on PC they overwrote `spC` (and GCC may drop UB iterations) | `sp14[6]` |

## Read and left alone

The rest are false positives (the unset path cannot happen), reads of the next field of the same struct or static
table (same on every run and every process), or display-only values:

- Every path sets it (the switch covers every value, a guard repeats the setter's condition, a search over
  fixed positions always finds one): bootDll (PAL-only path), m401 `fn_2_A0B0`, m403/m424/m425/m438/m453 motion
  state machines (the flag is only used when the state changes, and every change sets it), m404 pixel sizes,
  m406, m407, m408, m409, m410, m412, m414, m415 `fn_1_4660`, m419 `fn_1_6D04`/`fn_1_B054`/`fn_1_F73C`, m420 map,
  m423, m426, m427, m431, m433, m434, m440, m444, m445 (`fn_1_3094` is never called), m459 `fn_1_6198`, m462,
  w02 gamble/gendice/roulette/mg_item, w03 condor, w06 bridge, w10, board/last5 `temp_r21` (some rank always
  exists), board/player.c `ExecJunction` (unset only in the tutorial's scripted input), board/item.c hammer,
  hsfmotion, saveload, window.
- The spline helpers (m415 `fn_1_7268`, m428 `fn_1_8F90`, m438 `fn_1_8C34`, m453 `fn_1_4E84`) are unset only for
  fewer than 3 points; every caller passes more.
- Display only: m419 `fn_1_344C`'s fly-away angle (also `lbl_1_bss_200[-1]` if nobody was under the item when it
  landed), m459 `fn_1_4FC8`'s record pulse, w04 boo alpha, board pause/roll/ui/mg_setup layout, last5 sound,
  hsfanim `angleStart`, hsfdraw `bumpTevStage`, hsfex camera, sprput `bg_bmp`, staff, option.
- Loops into the next field: m415 map (zeros `unk74`'s neighbours, inside the struct), m419 `unk06`, m420
  `unk_C4`, m425 `unk_3C`/`unk_50` (a union says so), m427, m428, m440 (`unk20` + `unk38` hold 9), m442/m453
  score (`unk_18`/`unk_1A`/`unk_14`/`unk_16` are sprites too), m446 cursor (`unkC[9]` is the padding before
  `unk20`, on PC as on GC) and stage, m449, mstory3 (`unk34[4][2]` is really `[12][2]`: 0x34 + 96 = 0x94, the
  struct's size), board/shop `shopMdlIdx[0][1]`, last5 `character[-1]` (`unk02`, and member 0 of the ticket's
  sprites: same in every process).
- Not in a party: E3setupDLL, mpexDll/ztardll `fn_1_140B0`/`fn_1_97D0` (N53 fixed their `var_r28`), modeltest.
- `include/ext_math.h` (two): inlined, GCC names no caller.

## Runs (GMPE01 Rev 1, `$PB_DISC`, fresh user dirs, `--turbo`)

| what | build | result |
|---|---|---|
| gdb, break on the free error (`memory.c:122`) | before | 21 errors at the first minigame's return (m415's shadow buffer, then 4 anims x 5 from `Hu3DModelKill(0)`) |
| gdb, break on frees of the anim (same addresses, ASLR off) | before | first free: m456 `fn_1_8C68` (stage.c:1518) from its `fn_1_BE4`; second: `omOvlKill -> Hu3DAllKill -> Hu3DModelKill` |
| gdb, the error's `block->retaddr` | anim fix | m415's error: the first free was `HuSprAnimKill+378` (sprman.c:480, `bmp->data`) |
| party.script to 62000, 2 runs | anim fix | identical; 1 free error left (m415) |
| **`party_check.sh`** | all fixes | **identical 135000 frames, 0 'memory free error' in either run**; exit 1: no `match over` (below) |
| party.script to 75000, the same files at HEAD (fixes reverted, rebuilt) | before | identical game state to the fixed build's first 75000 frames (`hashdiff`); heap layout differs from 8495 (m415, the freed block); 21 free errors |
| `--continue` from the run's turn-8 checkpoint, 2 runs, A taps; then A+L; then the stick circling | all fixes | identical 75000 frames each; all stay in the lottery |
| `--match 2@1500` (w03) under gdb, breakpoints on `fn_1_12C8`'s return and `fn_1_AF8` | all fixes | 135000 frames, 6 turns: no hit (nobody landed on the space whose land event it is) |
| `tick_gate_test.sh`, `match_test.sh`, `autosave_test.sh` | all fixes | pass |

**Why `party_check.sh` no longer ends.** In turn 8 (frame 69053) player 0, a human tapping A, buys a lottery ticket.
The ticket is scratched with the stick (the screen's "L" is the stick icon; lottery.c reads `HuPadStkX/Y`), so the
party waits there to the end. The unchanged build plays the same 75000 frames, so this is the current path of
`party.script`, not these fixes: N53's party (which ended at 120835) went another way before later tasks changed it.
A temporary frame capture (`aurora_capture_frame` from `netplay_frame_end`, removed) showed the scratch screen. A
stick that circles for 3000 frames after the purchase did not scratch either (same frozen state); the script needs a
proper input for it (filed).

So the fixes do not change the w01 party's game state, as far as it goes (8 turns, 8 minigames: 23, 42, 51, 62,
25, 20, 49, 38): none of the reachable uninitialized paths were taken, except where the value was the same.

## Not verified

- **w03 statue (`fn_1_12C8`)**: checked against the disassembly only. The w03 run never reached the land event
  in 6 turns, and the script got stuck after that (likely the same kind of input wait, not looked at).
- **mstory3 `fn_1_16924`**: by disassembly; the party never reached the results this time (lottery). The START path
  returns `HuAudFXPlay`'s 12 as before; the A path (detail pages) was not run.
- **The board and minigame COM fixes** (com.c, item bag, w02 coin block, w04 item minigame, m416, m419, m435, w05):
  not run. None of these boards or minigames was in the runs, apart from w01's item use (com.c), whose state matched
  the old build. Their GC values come from the disassembly (com.c, w02) or are unknowable (the rest); a fixed value
  is deterministic, the old one was not, but whether a COM now plays exactly like GC's cannot be checked here.
- **m417, m443, m447, ztardll, m415 (shadow)**: m415 ran (no error, and the shadow anim showed nothing odd in the
  logs; no picture taken); the others did not run.
- **The texture-loop mask (sprman.c)**: no crash or difference was seen before it either; the fix is from reading
  the code.
- **A full `party_check.sh` to `match over`**: see above; needs a lottery input in `party.py`.
- **Two lockstep peers**: no lobby run (as in N53, two processes = two ASLR layouts, which these runs had).
- **Windows/Android/Apple**: not built (no toolchains). arm64 returns in x0 and the first argument is also x0,
  so some of the leftovers differ there, but the fixes do not depend on it.
- **`include/ext_math.h`'s two warnings**: GCC names no caller; not traced.
