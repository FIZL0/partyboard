# N115: the tutorial board's last5 crash came from N107's shortcut

N107 booted the tutorial board (w10) with a temporary bootDll shortcut (`tutorial-hook.diff` in
`research/journal/2026-10-28-n107-wasm-fnptr-audit`). The board crashed right after its 11th `TutorialHookExec`
(SCEN 7, CUE 4), in `last5.c` `InitHost` -> `BoardModelMotionCreate` -> `Hu3DJointMotion` -> `LoadHSF` ->
`HuMemMemoryAlloc2`: SIGSEGV on the desktop, `memory access out of bounds` in the browser, at frame 1197 on both.

**Verdict: the shortcut was wrong. The game is fine, and nothing in `src/` changes.** The real tutorial never
starts the last-5-turns event. The shortcut copied selmenuDll (the debug menu), which only sets flag 0x1000B and
calls w10. It never called `BoardSaveInit(6)` or set `GWSystem.max_turn`, so at a fresh boot `board`, `turn` and
`max_turn` were all 0.

## Why it crashed

- board/main.c runs `BoardLast5Exec` at the start of a turn when `CHECK_LAST5_TURN()`,
  `(s32)(max_turn - turn) < 5`. With `max_turn` 0 that is true on the tutorial's first turn. SCEN 7 / CUE 4 is
  `TutorialGameStartExec`, the start of the first turn, so last5 runs right after the 11th hook call.
- last5 isn't authored for w10. `hostMotTbl` rows 6..9 (w10, w20, w21, ...) are all `0`, and `messBaseTbl[6..8]`
  is `0` too. `InitHost` asks `BoardModelMotionCreate` for data number 0, `LoadHSF` reads a file that isn't a
  motion, and the allocation it sizes from that file walks off the heap. Setting only `max_turn = 0` (board 6,
  turn 1, as mentDll sets them) gives the same crash at the same frame (1197) and the same backtrace. So does
  restoring the shortcut's board 0 / turn 0 / max_turn 0.
- By the same logic the GameCube debug menu would crash the same way unless an earlier board left `max_turn` set
  (inferred from the code, not run). It is not a port bug.

## How the real tutorial is set up

Party Mode (modesel case 0) -> `omOvlCallEx(DLL_mentdll, 1, 0, 0)` -> mentDll `fn_1_B654` -> `fn_1_88A4`, Toad's
"tutorial?" question (`fn_1_1DD8(0x1E0035, 3, 0)`, a `HuWinChoiceGet` with the cursor starting on choice 1). On
choice 0 it sets flag 0x1000B, characters 3/0/2/4 (all COM, diff 0, group 0), **`BoardSaveInit(6)` (turn = 1)**
and **`GWSystem.max_turn = 20`**, then `fn_1_C174(2)` calls w10 (overlay 95). N106's `BootFcoJump` (committed,
`-DPB_FCO_BOOT` builds only) already does the same for w10 (BoardSaveInit + 20 turns). That is why N106's
150000-frame w10 sweep run never crashed.

## Runs (desktop, GMPE01 Rev 1, `--turbo`, all COM, A on every port every 20 frames)

Harness here: `run.sh OUT [gdb file]` (bootDll's `PB_FCO_OVL` jump, a `-DPB_FCO_BOOT` build in `build/n115`,
`OVL=` picks the overlay), `tapa.py` (the pad script). Each gdb file prints `N115 ...` lines into `OUT/out.txt`.

| run | set-up | result |
|---|---|---|
| A | `OVL=95`, BootFcoJump (mentDll's board/turn/max_turn) | all 41 scenes (97 hook calls, SCEN 40 the last), w10 returns, 60000 frames, no crash |
| A2 | A plus `ment.gdb` (mentDll's characters 3/0/2/4, diff/group 0) | the same 97 hooks. `BoardTurnNext` and `BoardLast5Exec` are never hit. Returns with board 6 turn 1 max_turn 20 |
| B | `shortcut.gdb`: board/turn/max_turn back to 0/0/0 (N107's state) at `omOvlCallEx(95)` | **11 hooks, SIGSEGV in `InitHost` (last5.c:449), frame 1197**, N107's crash |
| C | `maxonly.gdb`: only `max_turn = 0` | the same crash, frame 1197 |
| R | `OVL=70` (mentDll event 0, the real Party Mode entry), `real.gdb` | mentDll asks the question. With the taps alone it answers **No** (cursor starts on choice 1) and goes on to a w01 party. With `real.gdb` moving the start cursor to Yes in `HuWinChoiceGet` (what a player's stick-up does): mentDll's own code calls w10 with board 6 turn 1 max_turn 20, and the tutorial plays to the end **four times in a row** (97 hooks each, back to mentDll and asked again), plus a fifth start when the script ended at 60000. No last5, no turn advance, no crash |

## Tried and dropped

- `OVL=66` for mentDll: that's m460 (the header's two `DLL()` lists threw off counting by hand). `gdb -batch -ex
  'print (int)DLL_mentDll' build/n115/libdol.so` gives 70.
- A breakpoint on the static `BootFcoJump` never hit (inlined). The gdb files break on `omOvlCallEx` instead.

## Not verified

- **The menu from the title screen**: run R starts in mentDll (bootDll's jump, Party Mode's event 0), not from
  modesel's menus. The modesel case 0 set-up it skips (`GWMGExplainSet` etc. from the save's party settings)
  doesn't touch board/turn/max_turn. The Yes answer is gdb moving the start cursor, not a stick press.
- **Browser**: no web run. The crash and the setup are game logic, and N107 found the desktop and browser hash
  logs equal for the crashing run. Not worth a web build for a verdict that changes no code.
- **A human-played tutorial** (`PB_FCO_HUMAN`): all runs are all COM, as mentDll sets the tutorial up (iscom 1
  for every seat). A human can't change the turn counter the tutorial uses.
- **Android/iOS**: nothing platform-specific here.
