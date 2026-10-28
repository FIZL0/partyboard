# N83: m440 (ovl 48) not deterministic between two runs with the same pads

## Cause

`fn_1_C944` (src/REL/m440Dll/object.c), a player's turn/aim step in state 3, starts with

```c
if (fn_1_17CC() < -1) {          /* the shared 5 s choose timer has run out */
    fn_1_1708();
    HuPadStkX/Y = 0; HuPadTrigR = 0x96; HuPadBtn = 0x20;   /* force the throw */
} else {
    var_r29 = HuPadStkX[...]; sp8 = HuPadStkY[...];
}
if ((arg1->unk48 & 8) == 0) {
    if (ABS(var_r29) < 7) ...    /* stand still */
    else ...                     /* start turning left/right (unk48 bits 1/2) */
```

so once the timer runs out, `var_r29` is read uninitialized (GCC: `-Wmaybe-uninitialized`, via ext_math.h's ABS). On PC
it is stack garbage: on some runs it is >= 7, the player starts turning, its angle `unk68` and model differ, and the
COM AI (`fn_1_C1D4` steers by `unk68`) writes other pads. The timer only runs out for a player who never throws,
which is the scripted human (A taps, never R) in party.script.

What the GC did (GMPE01 m440Dll.rel, disassembled with N69's `ciso_extract.py` + `ppcdis.py`): `fn_1_C944` saves
r25..r31 (`_savegpr`) and keeps `var_r29` in r29 (`lbz 29` / `extsb 29` at 0xCA24 is the else branch), so on the
timeout path r29 is the caller's. The only caller, `fn_1_B3A4` (bl at 0xB7BC; 0xB7A4/0xB7B0 call fn_1_16D8 and
fn_1_C1D4, which restore r29), keeps its player loop index in r29 (`li 29, 0` at 0xB3C0, `addi 29, 29, 1` at 0xB854).
So on the GC `var_r29` is 0..3 there and `ABS(var_r29) < 7` always held: the player stands still.

## Fix

object.c, `#ifdef TARGET_PC` in the timeout branch: `var_r29 = 0; sp8 = 0;` (0 is the stick the branch just wrote;
same outcome as the GC's 0..3). Offline: same as GC; before it was random per process.

The other candidates in the task:
- object.c:1152 `var_r28` (fn_1_F228): set by a switch over `GWPlayerCfg.diff`, which is always 0..3 (match seats,
  menus). False positive, left alone (N69 said the same).
- main.c:1441/1775 `unk20` over `unk38`: already fixed by N73 (`unk20[9]`).

## How it was found

1. N73's boot hook (`harness.diff`: `PB_N73_OVL=48` with `backend.skipBootSequence`) straight into m440, plus two
   temporary knobs in the same diff: `PB_N83_HUMAN` (player 0 human, COMs diff 1, as `--match`'s seats) and
   `PB_N83_FILL=<seed>` (HuMemMemoryAlloc2 fills new blocks with per-seed garbage), and `PB_N83_FR` (log every
   frand/frandmod caller with frame and return address). `run.sh` runs one; `tap.py` writes the script (party.script's
   A taps, same phase). Config: copy a run's config.json and set `backend.skipBootSequence: true`.
2. 4 COMs, no human, 30 runs: all identical (no COM lets the timer run out).
3. With the human: runs split into two outcomes, the second first differing at **frame 862** with exactly the party's
   signature (`frand` drawn on one side only, then `pad`): 10 of 16 heap-filled runs, and 9 of 20 runs with no fill
   (plain ASLR); also under gdb (both with and without ASLR, depending on the run). So not heap garbage: stack.
4. `PB_N83_FR`: the extra frand draws are `UpdateCharAnim` (chrman.c:337/360, run-dust effect), i.e. a character
   moving on one side only. That pointed at the player state, and GCC's only uninitialized-read warning in m440's
   player code was `var_r29` in fn_1_C944.

## Runs (GMPE01 Rev 1, `$PB_DISC`, `--turbo`, fresh user dir each)

| what | before fix | after fix |
|---|---|---|
| hook, 4 COMs, `end 8000` | 30/30 identical | - |
| hook, human + 3 COMs, heap fill seeds | 10 of 16 differ at 862 | 16/16 identical |
| hook, human + 3 COMs, no fill | 9 of 20 differ at 862 | 20/20 identical |
| hook, under gdb (ASLR on/off, fill) | differ at 862 (some) | 3/3 identical |

The fixed runs match the pre-fix "no turn" outcome (identical to the pre-fix reference run for 8000 frames).

`tools/netplay/party_check.sh`, two pairs after the fix: **all four runs identical for 135000 frames** (checked
across pairs too), all reach `match over` at 110988. m440 runs 76339..81550 in it. In N79's pair 1 the runs ended
at 110988 and 116948 (the m440 split changes the rest of the party); N79's pair 2 ended both at 116948, so that
pair had taken the other m440 outcome on both sides.

## Tried and dropped

- Looking for the extra draw with gdb backtraces on `frand`: every `frand()` call is at setup (particles); the
  in-game draws are `frandmod`, which needed the logging hook. gdb also changes the outcome from run to run, so
  the comparison was done without it.
- `netplay_frame()` in the log looked stuck: the logged frame is lockstep's; the hash-log's frame is the line number.

## Not verified

- **The original pair's frame 78195**: that log is gone except for 16 lines; I did not replay that exact party. The
  signature (frand first, then COM pads, sys/plr equal) and the human-never-throws timeout match; the party runs above
  are the check that it does not recur.
- **Two lockstep peers / Windows / Android / Apple**: not run. Clang (Android/iOS) has the same uninitialized read
  and gets the same fix (plain C, no compiler-specific part).
- Other overlays with the same "timeout branch skips the stick read" shape were not searched for.
