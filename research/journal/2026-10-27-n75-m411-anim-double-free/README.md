# N75: m411 freed its made anims twice

## Cause

m411 (ovl 19) makes eight anims with `HuSprAnimMake` (`useNum` 0) and gives each one to `Hu3DAnimCreate`, whose
`HuSprAnimRead` counts it (`useNum` 1). At its end (main.c, `omSysExitReq` path and the normal end) m411 frees the
shared screen copy `lbl_1_bss_0`, nulls each anim's `bmp->data` and calls `HuSprAnimKill`: `useNum` 0, so the anim is
freed while its texture anim still exists and draws with it. `omOvlReturnEx` -> `omOvlKill` -> `Hu3DAllKill` ->
`Hu3DAnimKill` then decrements the freed anim's `useNum` and frees it again (`HuSprAnimFree` sees a block that is
not used any more and only passes it to `HuMemDirectFree`): one `HuMem>memory free error` per anim, 8 per play.
GC does the same (one free for the made anim's single block instead of N57's five, which `HuSprAnimFree` already
skipped). The double free was harmless only because nothing allocated in between; the models also drew from freed
anims for the frames between m411's kill and `omOvlKill`.

m411 is the only code that hands a `HuSprAnimMake` anim to `Hu3DAnimCreate` (grep: m415, m438, window.c, boo.c make
anims but use them as sprites only).

## Fix

The N69 shape (an anim a particle still draws with), for texture anims:

- `Hu3DTexAnimUseCheck(anim)` (hsfanim.c, TARGET_PC): TRUE if a live `Hu3DTexAnimData` entry holds the anim.
- `HuSprAnimKill`: after the `ANIM_BMP_ALLOC` bitmap frees (the owner's data, as on GC; m411 has nulled it), an anim
  a texture anim still holds is left alone. Its last `Hu3DAnimKill` (already reached `useNum` <= 0) frees it.
- `HuSprAnimFree` (that `Hu3DAnimKill` path) now also destroys the anim's texture objects
  (`HuSprAnimTexDestroy`, the loop moved out of `HuSprAnimKill` unchanged). Without that the deferred kill would
  have leaked m411's eight aurora texture objects, which `HuSprAnimKill` used to destroy. Side effect: every anim
  freed by `Hu3DAnimKill` (file anims of any texture anim) now releases its texture objects too; before, they were
  never destroyed. GPU side only, not in any hash.

## Checks (GMPE01 Rev 1, `harness/`)

`harness/instrument.diff` (temporary, reverted): N57's bootDll jump straight into an overlay with 4 COMs (`N75_MG`)
and the one-process mid-play restart (`N75_RESTART=<ovl>:<frames>:<times>`). `harness/run.sh <tag> <ovl>` runs it with
`--script mg.script --turbo --hash-log`.

| run | code | free errors | result |
|-----|------|-------------|--------|
| m411 straight, 9000 frames | before | 8 (right after `Ovl Return`) | final `4417FEB11D2590FF`, heap `2AF2187B787660D3` |
| same | fix | 0 | all 9000 hash-log lines identical to before, heap hash included |
| m411 killed 600 frames in, twice, then played to its end, 11000 frames | before | 8 (at the end only) | final hash as above |
| same | fix | 0 | final hash as above (heap `2AF2187B787660D3`: the kills and the deferred frees leave nothing behind) |
| m428 (ovl 36), ovl 52 straight, 9000 frames | fix | 0, 0 | |

N57's r5 extra error inside a resumed m411 (`d1d9d9f0`) did not show up in the one-process restarts, before or after
the fix (only the 8 at the end). r5 was N57's code before its fix 5; r6 (fix 5) did not have it either.

`tools/netplay/party_check.sh` (fix): both runs identical for all 135000 frames, 0 free errors, no crash or GPU
error, through 8 minigame overlays (23, 42, 51, 62, 25, 20, 49, 38) with their texture anims killed at
`omOvlKill`. No `match over`: the party waits in the turn-8 lottery scratch as in N69 (N77), not this change.

## Not verified

- A resumed m411 between two real lockstep peers (N57 r5's case): round 1's minigame is random, and the two-peer kit
  needs many tries to land on m411. The one-process kill/restart covers the same kill + `omOvlGotoEx` path.
- m428's "1 per kill" and overlay 52's "8 per kill" from N57's sweep: those are logged on mid-play kills (N57's
  `sweep.sh`), not on a played-through minigame (0 here, straight play). Not part of this task and not re-run.
- That the texture objects are really released in aurora (no counter for them); checked only that nothing crashes or
  logs a GPU error in the runs above.
- Other platforms (Android, Windows/macOS/iOS): same C code, not run.
