# N52: resync to a minigame's start, and N46's two-peer check from a turn point

**What landed.**
- A desync inside the end-of-round minigame now goes back to that minigame's start (instDll), not to the last turn
  start before it. Before, that could be up to ~1.5 min back.
- N46's two-peer lobby check was run from turn points. It found a crash, now fixed: a reload mid-roll left the old
  board's dice in roll.c's statics.

## What was done

**Minigame resync point** (`src/port/netplay_resync.c` `netplay_minigame_point`, called from `netplay_ovl_start`):
- The point is taken when instDll starts after the board, during a lockstep match, with flag (1,2) set (mg_setup's
  end-of-round handoff, not a battle (1,4) or Bowser (1,3) minigame).
- The point is the SLSaveBoard blob, then `"PBMG"`, then `GWPlayerCfg[0..3].group`. It goes to
  `pbnet_resync_point` like a turn point, in memory only (no online save is written for it). The Rust side takes a
  blob of any length up to 16 KiB as hex, so nothing changed in `net/`.
- Why only (1,2): battle and Bowser minigames come back into the board's event code in the middle of a turn
  (`ExecTurnStart` (1,3)/(1,4)). That code may read dol statics set before the minigame (battle.c's coin pool etc.),
  which a reload cannot rebuild. Their resync still goes to the turn point before them, as in N46.
- Why the groups: mg_setup writes each player's side for a 1-vs-3 or 2-vs-2 minigame into `GWPlayerCfg[].group`.
  That is not in the blob, and SLLoadBoard sets group from `GWPlayer.team` (0 outside team play). The first run
  (below) crashed both peers in m420's setup (`fn_1_198`: `lbl_1_bss_18[1]` NULL, every group 0).

**Resume into it** (`src/port/netplay_match.c`):
- `netplay_match_continue` sees the mark (`netplay_minigame_blob`, in `netplay_save.c` next to the other blob
  helpers). It keeps the board blob only, plus the groups, and does not arm N46's turn resume.
- `MatchStartProc` builds the history boot, modesel, mstory3, then **the board** (`omovlhisidx` 3, event 0, as the
  board's own `omOvlCallEx` leaves it). It sets the white wipe mg_setup uses and calls `DLL_instdll`. instDll renames
  its own entry to resultdll, the minigame returns there, and resultDll returns to the board. The board starts with
  (1,2) and runs `BoardTurnNext`, as after any minigame.
- `netplay_ovl_start` waits for instDll, not the board, to do N38's state set and heap scrub. `resume_config` puts the
  groups back.

**Dice of an old board** (`src/game/board/roll.c` `BoardRollNetplayReset`, called in that same overlay start):
- `BoardRollKill` only marks the dice paused, and `DiceMain` frees them on the next frame.
- A reload replaces the board before that frame. `diceMdl`/`diceEff`/`diceObj` then still name freed models,
  particles and objects.
- The next roll's `DiceMain` used the stale `diceEff[0] == 4` (`DiceHideEffect` -> `Hu3DParManModelIDGet`): both
  peers crashed at epoch 1 frame 88 (run T below). The reset sets them back to their initial values.

## Tests

- `tools/netplay/match_test.c`, new block (N52). `netplay_minigame_blob` parses a point and gives back its groups. It
  refuses a turn blob, a bad mark and a short trailer. A continue from a point:
  - restores the board blob and the groups at the tick
  - frame 0 calls instDll with the board at history[3] and the white wipe
  - the board's start does not scrub; instDll's does (state again, dice reset, no new point)
  - after minigame -> results -> board, the next instDll takes a point
- `tools/check.sh` passes (see the end).

In-game: two `partyboard` peers with `$PB_DISC`, each in its own user dir under `build/n52run/`, in a private
netns, with N38's scratch harness (lobby buttons pressed by label and pads for this machine's seat: A every 24
frames, START in instDll and the results). The harness is not committed: `n38_harness.inc` + `netplay_c.diff` were
applied for the runs and reverted. `ns.sh` / `pair.sh` here start the namespace and the pair. The guest runs with
`--desync-at <f>`. Board 0, 20 turns, 2 humans + 2 CPUs.

| run | desync | what happened |
|---|---|---|
| cal | none | Turn points at 2740/3757/5264/5949, minigame point at 7418, m420 7692-9540, results, board. Identical 9968 frames. |
| m | 8500 (in m420) | Host: "everyone resyncs". **Both: "continues from a checkpoint at a minigame's start"**, instDll, m420, then **both crashed in m420's setup** (groups, above). |
| m2 | 8500 (in ovl 22) | Both resume into instDll. Then ovl 22, resultDll, board: turn 2 points at 3123/4246/5470. **Epoch 1 identical 5478 frames**, no further desync. |
| t | 4000 | First two-peer resync from a turn point (player 1, 3837): **both crashed at epoch 1 frame 88** (dice, above). |
| t2 | 4000 | After the dice fix: resumes at player 2's turn. Players 2, 3, instDll, ovl 20, results, turn 2. **Identical 4529 frames** (the guest's log is 1 line short, killed). |
| t3 | 2780 | Reload ~100 frames into player 0's roll. A temporary log showed the host's old die still live (`diceMdl[0] 49`, `diceObj[0]` set) when the reset cleared it. Player 0's turn replays. Turns, instDll, ovl 30, results, turn 2. **Identical 7043 frames**. |

(`hashdiff.py` on the epoch-1 part of each `--hash-log`, cut with N38's `split.py`. Each peer logged one desync.)

## Tried / failed

- First minigame resume: m420 crashed in its setup on both peers. The groups were not in the blob. Fixed with the
  trailer.
- First turn-point run: crash in `DiceHideEffect` on both peers. Fixed with `BoardRollNetplayReset`.
- Which minigame comes up depends on the run (runs are not deterministic across processes, N53), so a team
  minigame could not be picked for the in-game check. See Not verified.

## Not verified

(filled in at the end)
