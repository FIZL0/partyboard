# N65: mgBoardHostEnableF in a match

## Finding (from N52)

instDll (the minigame explanation) picks its host from a dol global (`src/REL/instDll/main.c:123`):
`instHostNo = GWSystem.board`, or 1 if `mgBoardHostEnableF`. modeseldll sets it to 0, mgmodedll and ztardll to 1.
A match never set it, so a peer that last left Minigame mode (or the match's reload after one) showed host 1 in
instDll while its peers showed the board's host.

## Change

`party_config` and `resume_config` (`src/port/netplay_match.c`) set `mgBoardHostEnableF = 0`, modeseldll's value.
Both run at the tick and again when the board/instDll starts (N38's state reset), so new matches, continues and
resyncs all get it. Only match code changed: offline play is unchanged by construction.

`tools/netplay/match_test.c`: a stub starts at 1 (Minigame mode's value). The new-party tick and the continue tick
must leave it at 0. On the old `netplay_match.c` the test fails at the first assert (checked).

## In-game check

One peer, `--match 0@900 --turbo --hash-log`, `$PB_DISC`, each run in its own user dir under the scratchpad (only
`video.enableVsync: false`, plus `backend.skipBootSequence: true` for the mg runs). The scripts were made with
`tools/netplay/save-continue.py`, changed in a scratch copy only: a `savemg` mode with head `wander-mgmode.script`
(N26: Minigame mode, ovl 72, at frame 900), `end 9500`, START window 7560-7720. A temporary
`OSReport` of `instHostNo` in instDll was used for these runs and then removed. Path: board 89 at 902, instDll (ovl 3)
at 7465, minigame ovl 17 at 7748.

| run | before the match | instDll's host | vs |
|---|---|---|---|
| old-title | title screen | 0 | |
| old-mg | Minigame mode | **1** | fix-mg: game state identical for 9500 frames, **heap layout differs from frame 7466** (instDll's first frame) |
| fix-title | title screen | 0 | old-title: identical for 9500 frames, heaps too |
| fix-mg | Minigame mode | 0 | fix-title from 900: game state identical for 8600 frames |

The gameplay hash does not cover the host model; only the heap layout shows it. fix-mg and fix-title have different
heap layouts from frame 900 on, but that is N26's known menu-path leftover (blocks the menus left in SYSTEM/DATA),
not this flag. So the heap comparison that isolates the flag is old-mg against fix-mg: equal up to instDll and
different from instDll's first frame.

## Not verified

- **Two peers in a lockstep match** (one coming from Minigame mode). The one-peer runs show the host choice and the
  heaps. A two-peer run needs N52's netns harness, and the single-peer runs already show the cause and the effect.
- **ztardll** also sets 1. The fix covers it the same way, but it was not run.
- **Screenshots of the two hosts.** Only the log value and the heap layout were checked.
- Windows/Apple/Android not built (no SDKs).
