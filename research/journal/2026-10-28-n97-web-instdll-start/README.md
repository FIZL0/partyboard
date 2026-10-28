# N97: browser match leaves to the menu at a minigame's instructions

## Cause: the title screen's process, not instDll (fixed by N99)

Run 1 (`tools/web_pair.mjs`, this branch's base 1e2e9af3, 480 s, WEB_FAST): reproduced on the first minigame. Tab A's
console, in order:

```
netplay: match start: killed 0 menu processes            (31.9 s, the start from the title)
netplay: match start: killed 0 menu processes            (131.9 s, a resync reload)
Start New OVL 3                                          (instDll, frame 1517 after the reload)
netplay: resync point at frame 1517: minigame 7 ...
objman>Call New Ovl 74(1)                                (164.5 s, after the harness's START at +300 frames)
netplay: match over: overlay 74 starts at frame 1952
```

`Call New Ovl` is `omOvlCallEx`, not instDll's `omOvlReturnEx`: nothing in instDll (or the board, or netplay's
match leave) calls an overlay that way into modeseldll. The only callers of `omOvlCallEx(DLL_modeseldll, ...)` are
bootDll's (`src/REL/bootDll/main.c:163` and `:386`, the title's "press START"). So `omSysExitReq` was never set: the
title's `BootExec` process was still running under the match, waiting for START, and the harness's START at the
instructions was the first START any pad pressed since the match began. It then called modeseldll, and netplay saw
the menu start as the end of the match (same frame and hash on both peers: both tabs started from the title, both
had a BootExec waiting, both got the START through lockstep). Why the BootExec was alive: "killed 0 menu
processes". `MatchStartProc` kills the menu overlay's processes by asking `dladdr` which module holds their code,
and the web build has no dladdr (RELs are linked statically into the one wasm). That is exactly N99's finding (its
heap split was the same surviving BootExec), fixed in N99 (merged on main as e2d8c29b, after this branch was cut):
`web_rel_function_owner` finds a function's REL from mark functions, so the web start kills it ("killed 1").

This also explains N92's observations: "~100 frames after the START" (the call waits for bootDll's wipe-out), "5 of
6 minigames" (run 6's first minigame started because the title's BootExec was elsewhere in its loop, e.g. the demo,
when the START came), "a START in the intro or twice" (any START at all), and "netplay's match leave didn't log"
(it wasn't one). B, Z, the system pause, `mgInstExitEnableF` and SDL's b9 mapping were all fine.

No game code change in N97: N99's fix is the fix. What N97 adds is `tools/web_pair.mjs`:
- the comments that blamed START timing ("a START in the intro, or a second one in its wipe, goes back to the
  menu") now say what it was;
- a run whose match ends at the menu without the party's results (`netplay: match over: overlay N starts`) FAILs;
- the summary line `reached:` gives each tab's highest turn (resync points), how many minigame overlays started
  (4..69 in `include/ovl_table.h`), and each start's "killed N menu processes" (N99's guard: a start from the
  title must say 1).

## The web build was broken on this branch's base

`tools/web.sh` failed in fmt (`malloc` undeclared): a86810a5 (N86, iOS IPA) had taken N87's web lines out of
`CMakeLists.txt` (`include(cmake/web_early.cmake)` with its `-includecstdlib`, the library types,
`include(cmake/web.cmake)`). N99 found and fixed that too (on main). I first restored the lines here myself, then
dropped that edit so as not to merge a second copy against N99's.

## Check with N99's fix

To test on top of main's fix without merging main into this branch, main's diff from this branch's base
(`git diff 1e2e9af3 e2d8c29b`, N99's merge) was applied to the work tree uncommitted, `WEB_FAST=1 tools/web.sh`,
and the pair run again; afterwards the patch was taken out again (by hand: `git apply -R` refused once
`run plan add` had written N104 into the same plan/TODO.md), so this commit holds only the harness, the plan's new
tasks and this journal.

- **Run 2** (N99 applied, 1800 s): `killed 1 menu processes` on both at the start. Every START at the instructions
  started the minigame: turn 1 (minigame 6, overlay 15), turn 2 (overlay 11), turn 3 (groups 1101, overlay 30),
  each with its results and the next turn; turn 4's points at 27175-28695 (N92's furthest was turn 2's
  instructions). No desync, no wait, 109749 frames on both, PASS. But from frame ~28.7k on it stood in turn 4's board
  lottery: Luigi (a human seat) at the ticket scratch, "L Scratch Off", which only the stick moves, and the fake pad
  has none (the same hang as scripted runs without `lottery-stick on`, N79). Filed **N104** (the pad plays the
  lottery's stick games). The new `reached:` line shows such a stall as "last turn/minigame point at frame 28695".
- **Run 3** (same build, 240 s, the new harness): turn 1, the START at the instructions started minigame overlay 19
  (m411), and both tabs aborted 0.3 s into it with a fatal WebGPU validation error, `SetViewport(1284, 20, -20,
  938)`: m411 itself sets a viewport of width -10 (`src/REL/m411Dll/main.c`, fn_1_8DBC / fn_1_8A80, the split
  screen's hidden quarters), which the GameCube and the desktop's Dawn (validation skipped) let pass. Not N97's; filed
  **N105**. (The harness then stopped on a screenshot that got no answer from the dead tab, so run 3 has no summary.)
- **Run 4** (same, 120 s, `SHOT_EVERY=1000`): `reached: a: turn 1, 1 minigames started, last turn/minigame point at
  frame 6431, menu processes killed 1; b: the same`, frames 7169/7171, PASS: the new lines run in the script.
- The `reached:`/menu-exit code on the logs of runs 1 and 2 (offline, the block cut out of the script into a node
  test): run 1 `turn 1, 0 minigames started, ..., menu processes killed 0,0; LEFT FOR THE MENU: a=1 b=1` (would FAIL
  now), run 2 `turn 4, 3 minigames started, last ... at frame 28695, menu processes killed 1` (PASS).
- `tools/check.sh` passes (desktop build, cargo test both feature sets). The commit changes only
  `tools/web_pair.mjs`, this journal and the plan (N104, N105).

## Not verified

- **N97 on this branch alone**: this branch's base doesn't have N99's fix, so on it the browser pair still goes to
  the menu at the first START (run 1). The fix was checked with N99's merge applied to the work tree (runs 2-4), not
  on a merged tree; the runner's merge of this branch onto main is what puts the two together (this branch touches
  none of N99's lines: web_pair.mjs's edits are away from N99's WEB_ROOT lines).
- **A whole party in the browser**: blocked by the lottery's stick games (N104) and m411's viewport (N105); turn 4
  of 20 is the furthest.
- **The desktop pair (N52's pair.sh) with the same START timing**: not run; the cause was found in the web build's
  log directly, and the desktop's MatchStartProc always killed the title's process (dladdr works there; N99 checked
  'killed 1' on a desktop pair).
- **PAIR_MENU starts** (the lobby through the Online menu): the new summary counts "killed N" from those starts as
  well, but no PAIR_MENU run was made here, so what N it prints there wasn't seen.
