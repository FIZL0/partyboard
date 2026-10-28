# N106: out-of-range float -> integer conversions on every board and minigame

N91 found that `(u8)negative_float` in m415 split the browser from the desktop: the conversion is undefined in C,
x86_64 (GCC) and arm64 wrap through a signed convert, wasm clamps unsigned targets to 0, and for `u32` targets arm64's
`fcvtzu` saturates negatives to 0 where x86 wraps. `tools/netplay/float_cast_check.sh` (GCC
`-fsanitize=float-cast-overflow` on all C) only ran party.script (w01 and 13 minigames). This task ran it over every
board and every minigame and fixed what it reported.

## Harness

- `tools/netplay/float_cast_check.sh` now takes, from the environment:
  - `FCO_MATCH=<board>@<frame>`: the `--match` (default `0@1500`, as before; 1..5 = w02..w06);
  - `FCO_OVL=<overlay index>` (+ `FCO_HUMAN=<seat mask>`): no `--match`, boot straight into that overlay;
  - `FCO_NOBUILD=1` for parallel runs of one build, `FCO_TIMEOUT` as before.
  It fails when the run logged no frames (a crash at boot passed before). Its build dir is now `build/n106-fco`
  (the flags changed: `-DPB_FCO_BOOT` too).
- The boot jump: `src/REL/bootDll/main.c` `BootFcoJump`, compiled only with `-DPB_FCO_BOOT` (the sanitizer build;
  every other build is unchanged). It is N73's temporary boot hook made permanent for this build: with
  `backend.skipBootSequence` (the script writes it into the run's config.json) and `PB_FCO_OVL`, bootDll sets four
  players (characters 0-3, COM unless in `PB_FCO_HUMAN`, difficulty = seat), minigame groups from `mgInfoTbl`'s type
  (1v3: seat 0 alone, 2v2: i/2), and calls the overlay. For a board (w01..w21) it first sets up a 10-turn party
  as mentDll does (`BoardPartyConfigSet`, `BoardSaveInit`), and for w10 the tutorial (flag 0x1000B, 20 turns, as
  mentDll's tutorial entry).
- This journal's `sweep.sh <out> [jobs] [list]` runs the whole sweep with `xargs -P` (run 2's list; run 1 also had
  overlays 4..8 and no `match0`): overlays 9..69 all COM
  (`com.script`: `end 12000`), the same with all four seats human on `wander.script` (random buttons, both sticks
  and triggers every 6-30 frames, from `wander.py`, no START), boards 89..97 (w01..w06, w10, w20, w21) all COM for
  150000 frames (`board.script`), and `--match 0..5@1500` with party.script (seat 0 human on A taps, the real party
  flow to the end). `summary.txt` has one line per run, `sites.txt` every reported site once.

## What the sweep found (run 1, before the fixes: `build/fco-s1`)

146 runs, GMPE01 Rev 1. Every minigame overlay that exists on PC (9..69 = m401..m463; 4..8, m300..m333, are not
built and crash at once), all COM and all human; every board w01..w06, w10 (the tutorial), w20, w21 all COM; the
`--match` parties on w02..w06. 16 distinct sites (GCC reports each source location once per process), all in
minigames or in drawing code. The boards themselves (src/game/board, w01..w21) reported nothing.

| site | value, type | what it is | platforms before the fix |
| --- | --- | --- | --- |
| m404 main.c:977/978 | -3.6, u8 (into s8) | a COM's stick from `20 * sind/cosd(angle)` | **gameplay**: x86/arm64/GC -3, wasm 0 |
| m404 main.c:1191/1192 | -7.0, u16 | bitmap pixel column/row left of/above the bitmap | x86/GC wrap (65529: `< sizeX` skips it), wasm 0 (draws in column 0) |
| m411 main.c:2323-2329 | -10, u32 | `Hu3DCameraScissorSet` sizes as the split screen opens | x86/GC wrap, arm64 and wasm 0 (render only) |
| m450 main.c:4021 | -1.5, uintptr_t (u32 on GC) | `work[0]` countdown below 0 | **gameplay**: x86/GC wrap (> 600: set to 1), arm64 0 (stays 0) |
| m401 main.c:2301 | 256447, s16 | third read of the 2-entry `lbl_2_data_424` (out of bounds) | value from whatever the compiler put after the table; a countdown nothing reads afterwards (`unk20` is 0) |
| m433 player.c:2676 | 255, s8 | particle alpha (`emitCnt * 255 / 60`) | same everywhere (in s32 range), undefined |
| hsfdraw.c:797/1076 (+1357, 1660) | 32640, u8 | material alpha `255 * (1 - invAlpha)`, invAlpha -127 | same everywhere, undefined |
| sprput.c:78/133 | 22440 / 5.7e6, u8 / u16 | sprite alpha far past 255 | same everywhere, undefined |
| aurora OSFastCast.h:15 via hsfdraw.c:2497 | NaN, s32 | hilite: `VECDotProduct` with a NaN light direction (m429, m446) | x86/GC INT_MIN, arm64/wasm 0; the s16 is 0 on all |

Values inside s32's range but outside a narrower type (the last four rows) wrap alike on every platform, so they
only matter as undefined behaviour (a compiler may assume they don't happen). The ones that split platforms are
negatives into unsigned types: wasm clamps every unsigned target to 0, arm64 clamps 32/64-bit unsigned targets.

The board runs: w01..w06 boot jumps each played 10 turns (9-14 minigames each) to the party's end; w10 ran the
tutorial for 150000 frames; w20 (no minigame) ended at 49216, w21 (one, m436) at 56706. `--match`
parties: w01 (`match0`, in run 2), w02, w04, w05 reached `match over`; w03 and w06 stuck in m444 (below).

## Fixes

Through s32, as N91's m415 fix: `(T)(s32)f` is defined and wraps like the GC's `fctiwz` + `stb`/`sth`/`stw` and x86.
- m404: the COM stick (`(u8)(s32)`, now -3 everywhere as on the GC) and the bitmap column/row.
- m411: the four scissor calls pass `(s32)(...)` to the u32 parameters.
- m450: `work[0] = (u32)(s32)(...)`, the u32 the GC has (on x86 it was 0xFFFFFFFFFFFFFFFF, now 0xFFFFFFFF; both > 600).
- m433, hsfdraw (all four material-alpha lines, 1357/1660 are the same expression, not reached by the sweep), sprput.
- m401 (PC only): the third read takes what the GC reads there, `lbl_2_data_42C[0]` (the next table in m401's .data),
  and wraps through s32. GCC on x86 read something else (~589830 from its own layout), wasm would read yet another
  thing; the value is dead, but now it is the same everywhere.
- hsfdraw.c hilite (PC only): a NaN `dot` is set to 0 before `OSf32tos16`, which is what every platform's s16 came out
  as (GC/x86 INT_MIN, low half 0; arm64/wasm 0).

**Desktop unchanged:** each changed file compiled at HEAD and now with the build's own command (`ninja -t compdb`),
objdump compared: m404 and hsfdraw have the same `cvttss2si` conversions (register allocation moved; hsfdraw gains
the NaN compare), m433/sprput/bootDll (without `-DPB_FCO_BOOT`) are byte-identical, m411's six 64-bit `cvttss2si`
(float -> u32 through s64) are 32-bit ones now (same low 32 bits for these values), m450's float -> u64 pair is one
32-bit convert. So x86 computes what it did, except m401's dead value and m450's high 32 bits (overwritten with 1 in
the same frame).

## Tried and dropped

- An aurora patch 0010 making `__OSf32tos32` the GC's fctiwz (NaN/below -> INT_MIN, above -> INT_MAX) for every
  `OSf32to*`: it edits the lines 0009 edits, so on the next configure 0009 neither reverse-applies (0010 changed
  them) nor applies, and `cmake/aurora_patches.cmake` stops. Stacked patches on the same lines don't work with that
  mechanism; the one NaN site is fixed at its caller instead.
- `print_stacktrace=0` hides who called an inline helper: rerun one overlay with `UBSAN_OPTIONS=print_stacktrace=1`
  (the script now passes `UBSAN_OPTIONS` through) to get the caller (that is how the hilite NaN was found).

## Checks

**Result: after the fixes, `float_cast_check.sh` reports nothing on any of the 137 runs** (`build/fco-s2`, same
list as run 1 minus the five overlays that don't exist, plus the w01 `--match`): m401..m463 all COM and all human
(12000 frames each), w01..w06, w10, w20, w21 boot jumps (150000 frames each), `--match 0..5@1500` with party.script
(135000 frames each; w01/w02/w04/w05 to `match over` at 110987/120027/93247/113527 and on into the next session).

- **The desktop plays the same as before:** run 1 (before the fixes) and run 2 (after) give identical hash logs
  (`hashdiff.py`, every field incl. heap=) for all five `--match` parties (135000 frames), all nine board runs
  (150000) and every minigame that had a site, all COM (m401, m404, m411, m429, m431, m433, m446, m450, m451; 12000).
  Both runs are the sanitizer build, which converts like the normal one. The objdump comparison above says the same
  for the normal build's objects.
- `cmake --build --preset linux-default-relwithdebinfo` builds; `tools/check.sh` (run by `done`).

## Not verified

- **Web and arm64 runs of the fixed minigames.** The sites that split platforms (m404's COM stick: wasm; m450's
  countdown: arm64; m404's bitmap and m411's scissor: render) are fixed by the same through-s32 rule N91 checked on
  the web; no browser or Android run of m404/m450 was made (a web run is ~1 h per party; the fix keeps x86's values,
  shown identical above).
- **Code the sweep didn't reach.** The sanitizer only sees conversions that run: COM and random-pad paths of each
  minigame (one seed each), 10-turn all-COM boards with one seed, story mode (mstory*), Minigame mode's own
  games and menus not at all. Sites on other paths remain possible; `float_cast_check.sh` with `FCO_OVL`/`FCO_MATCH`
  runs any of them again.
- **m445 (ovl 53) after ~4100 frames:** both boot-jump runs dump core there (a call through NULL, after
  "Error: data none on ARAM 1a0018"). It is a type-6 minigame (special/story set-up the boot jump doesn't make);
  no board run picked it. Not investigated: most likely the jump's missing set-up, not a game bug.
- **m444 inside a party:** w03 and w06 `--match` parties and the w02 all-COM board sat in m444 until the end
  (78142 / 85344 / 38228 frames), so their later turns weren't swept. Filed as **N108**. m444 alone (boot jump) was
  swept, both runs.
- **m300..m333 (overlays 4..8):** not built on PC, nothing to check.
- The hilite NaN's origin (why a light's direction is NaN in m429/m446) is not traced: render only.

## Leftovers filed

- **N108**: m444 never ends inside a party (above).

## Reproduce

```
tools/netplay/float_cast_check.sh            # builds build/n106-fco, w01 party as before
research/journal/2026-10-28-n106-float-casts/sweep.sh build/fco-sweep 6   # ~2 h on a loaded 16-core box
FCO_NOBUILD=1 FCO_OVL=12 FCO_HUMAN=15 tools/netplay/float_cast_check.sh \
    research/journal/2026-10-28-n106-float-casts/wander.script build/fco/m404   # one overlay
```
