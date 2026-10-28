# N85: the port's HuAudFXStatusGet stub (12 for ever -> 0)

## Outcome

- The port plays no sound effects: `src/port/audio.c` stubs `HuAudFXPlay` (returns 5), the emitter, pan, pitch and
  volume calls, and `msmSeSetParam` (`src/port/stubs.c`, returns 0). Music is stubbed too (`msmMusGetStatus` 0).
  `HuAudFXStatusGet` returned 12 for every handle, for ever.
- On GC, `msmSeGetStatus` (`src/msm/msmse.c`) returns the SE player's status: 0 (`MSM_SE_DONE`) once the sound has
  ended or when no player has that handle, otherwise 1..3 (`msmSePlay` sets 2, a finished fade-out sets 1). 12 is not
  a status GC ever returns. So the stub said "playing" for a sound that does not exist.
- **Fix:** the stub returns 0, as `HuAudSStreamStatGet` has since b1c33527. 0 is the only true answer when nothing
  plays, and it is what GC returns once each sound has ended.
- **What changes vs GameCube, per caller** (every caller reads it; only w06's waits on it, fixed in N84):
  - **Board taunts** (`src/game/board/main.c` `TauntUpdate`): a non-current human's L plays the character's taunt
    and keeps its handle in `tauntActiveFXTbl[port]` until the status reads 0. With 12, the first taunt's handle
    stayed for the rest of the board: every later L was ignored (the branch only re-pans the "playing" taunt with the
    stick, via the stubbed `msmSeSetParam`). On GC a player taunts again once the clip has ended. With 0, the handle
    is cleared on the next frame and every L press plays a taunt again. The port has no audio, so nothing is audible
    either way; the difference is only which stub gets called.
  - **w01 `mg_coin.c`** (space amida, case 6): once the rocket landing sound (0x332) reads 1 or 0, it calls
    `BoardAudSeqPause(0, 0, 1000)` (resume the board music) once. With 12 it never did. In the port that call returns
    at `BoardMusStatusGet != 3` (`msmMusGetStatus` stub 0), so it only sets `spaceAmidaSeqStopF`, which nothing else
    reads but the same `if`.
  - **w03 `mg_coin.c`** (`lbl_1_data_450`, the looping sound 0x43E): cleared when the sound ends, so the next frame
    that needs it plays it again. With 12 it was only cleared by the explicit stop. With 0 the port "plays" it again
    every frame it is wanted (a stub call).
  - **m439** `fn_1_4CC0`: plays 1796, then plays it again while `lbl_1_data_7798` counts down (2), each time the last
    one has ended, and pans it every frame while it is "playing". With 12 the repeat never happened and the pan ran
    every frame until the object died (152 frames in the run below). With 0: one repeat on the next frame, then the
    handle goes to -1.
  - None of these values reach gameplay state: they are sound handles and one flag, and every call they lead to is a
    stub with no side effect. No RNG, no `GW*` state, no allocation.
- **Offline hashes do not change** (runs below).

## Checks (GMPE01 Rev 1, `$PB_DISC`, own user dirs under build/n85/runs)

Two builds, both with N73's boot hook (`harness/boot-hook.diff`, renamed `PB_N85_OVL`, temporary, reverted before the
commit): `base` (HEAD) and `new` (this stub), each copied to its own dir (RUNPATH `$ORIGIN` first). `harness/run.sh`
runs one `--script ... --turbo --hash-log`; `harness/cov.sh` counts gdb hits per line in one run.

- **m439 directly** (`PB_N85_OVL=47`, skipBootSequence, `end 30000`): base x2, new x2, all four hash logs
  byte-identical for 30000 frames (game state and `heap=`), minigame 4..4038 then `Ovl Return`.
  gdb hit counts (one run each): line 1230 (first play) 1 / 1; line 1235 (status check) base 152, new 2;
  line 1237 (repeat play) base 0, new 1; line 1241 (handle -> -1) base 0, new 1. So the changed path ran.
- **Whole parties** (`tools/netplay/party.script --turbo`, 135000 frames, 13 minigames on w01):
  - w01 (`--match 0@1500`): base x2, new x2. base-1 = base-2 = new-2, identical for 135000 frames, `match over` at
    116947. new-1 split at 78195 in m440 (ovl 48, from 76339): N83's known m440 nondeterminism, the same frame N82 saw
    split on its own baseline, so not this change.
  - w03 (`--match 2@1500`): base x2, new x2, all four identical for 135000 frames.
  - gdb hit counts (`cov.sh`, one run per build per board): `TauntUpdate`'s L play (main.c:2218) **0** in every run
    (seat 0, the only human, never pressed L), w01 `mg_coin.c:417/418` **0**, w03 `mg_coin.c:475/493/494` **0**: no
    party landed on those coin events. So these runs only show that nothing else moved.
- **Taunts, forced** (`taunt.sh`): the w01 party under gdb, and at the L check (main.c:2217) gdb sets
  `tauntActiveFXTbl[0] = 5` (what the stubbed `HuAudFXPlay` returns; seat 0 is port 0; `port` itself is optimized
  out), i.e. a taunt played every time the slot is free. Pads are not touched.
  - base: the check ran 6871 times (once per board load, `BoardTauntInit` resets the slots after each minigame), then
    the slot stayed "playing" and only re-panned (main.c:2209: 40801 hits; the clear at 2185: 0).
  - new: cleared 41852 times, a taunt again 47672 times, re-pan 0.
  - Hash logs: base-forced = new-forced = the unforced base party, identical for 135000 frames.
- **Tried and failed:** a `party.script` variant with pad 0 pressing L with every A (`party-L.script`, sed of
  party.script). L on pad 0's own turn puts the board in a mode the script never leaves (stuck in ovl 89 from ~20000
  to the end, in both builds, identical logs), and no taunt fired before that (2218: 0 hits). Replaced by the gdb force.
  First gdb force used `tauntActiveFXTbl[port]`: optimized out, the batch stopped.

## Not verified

- **w01 space amida and w03 `mg_coin.c` paths at run time:** no party reached them (0 hits), and I did not force
  them. They are covered by reading only: w01's call is `BoardAudSeqPause`, which returns at the `msmMusGetStatus`
  stub (0, not 3), and w03 only re-plays the stubbed `HuAudFXPlay`. A forced run (a boot hook into those events)
  would close it.
- **Two peers online:** not run. The changed values are per-machine sound handles that never reach state, and both
  peers run the same stub, so lockstep sees the same thing either way; no two-peer run was done.
- **The w06 Bowser wait with the new stub:** N84's `TARGET_PC` skip in `fn_1_A408` stays. With 0 the original wait
  would also end at once, so the skip could go, but I left N84's code alone (no run of that event here).
- **Real audio:** if the port ever plays sound effects through msm again, `HuAudFXStatusGet` must go back to
  `msmSeGetStatus`; the stub's 0 is only right while nothing plays.
- **Android/other platforms:** the stub is plain C with no platform code; only Linux was run.
