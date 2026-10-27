# N67: the board's start frame no longer depends on what a peer was doing at the match start

## Outcome

- **A rejoin before turn 1 (the host reloads inside the board, the guest from a menu) does not desync on the current
  code.** In every run, both peers' board overlay starts at frame 1, and the epoch-1 hash logs are identical, heap
  included (runs e1–e3, below). N56's r1–r3 desync at frame 30 came from N56's fix 1 bug (the menu lockstep steps),
  as N56 suspected. That one is gone.
- **The board's start frame still depended on the peer's state in one case, now fixed:** an overlay switch of the
  peer's own was under way when the match start applied. Reproduced in one process with `--match` aimed at mash-a's
  overlay switches:
  - `--match 0@994` and `0@1911`: omwatchproc was past its `HuPrcSleep(0)`, so it started the board **in frame 0**,
    one frame before every other peer.
  - `--match 0@995`: omwatchproc was asleep in modeseldll's AUX change (`HuAudDllSndGrpSet`'s `HuPrcVSleep`). It
    woke in frame 0, set `omcurovl = omnextovl` (by then the board) and loaded the board under modeseldll's setup,
    two frames early. `netplay_ovl_start` had run for modeseldll, so **the board's start scrub never ran**. It ran
    at the board's next start, after the round-1 minigame (frame 13563), and `party_config` then reset
    GWSystem/GWPlayer in the middle of the match.

## Why a running overlay is safe

`MatchStartProc` runs first in frame 0 (priority 0xFFFF). Its `omOvlCallEx` → `omOvlKill` → `HuPrcChildKill(omwatchproc)`
clears `omwatchproc->child` at once. omwatchproc waits in `HuPrcChildWatch`, so `HuPrcCall` wakes it in the same
frame. Its `HuPrcSleep(0)` then puts the board's start in frame 1. A board peer (N62's destructors, then the same
kill) and a menu peer go through the same steps. An idle omwatchproc (no overlay, nothing next) does too. The only
states that differ are the two in-flight ones above. `fadeStat` is always 0 in the port, so that wait does not
apply.

## Fix

- `netplay_ovl_hold()` (`src/port/netplay_match.c`) is TRUE in the frame in which `MatchStartProc` called the board
  (or instDll for N52's minigame continue), until the board's scrub.
- `omWatchOverlayProc` (`src/game/objmain.c`, `TARGET_PC`, `netplay_active()` only) asks after each of its two
  sleeps before an overlay starts: after `HuPrcSleep(0)`, and after `HuAudDllSndGrpSet`. If the answer is TRUE, it
  goes back to the top of its loop. The next `HuPrcSleep(0)` then starts the board in frame 1, as on every other
  peer. After the AUX sleep it first kills the sprite group that `espInit` made for the overlay that never ran
  (`HuSprGrpKill(espGrpIDGet())`). No `omOvlKill` comes in between, so without this SYSTEM kept one extra block (832
  B = 0x180 members + header), and the heap layout differed from a normal start.
- Offline is unchanged: both guards are behind `netplay_active()`. The hold is FALSE unless a match start switched
  overlays in this frame.

## Checks (GMPE01, `$PB_DISC`, relwithdebinfo, Linux)

### One process, `--script mash-a.script --turbo --match 0@N` (`harness/edge.sh`)

mash-a's overlay switches show as `ovl=-1` in the hash log: frames 993–994 (bootDll → modeseldll, two frames because
of the AUX sleep) and 1910 (→ mentDll). In the table, "start" is the board start frame minus the frame where the
match state was set (1 = a normal start).

| N | before | after |
|---|---|---|
| 991, 992, 993, 1908, 1909, 1910, 1500 (control) | 1 | 1 |
| 994 | **0** | 1 |
| 1911 | **0** | 1 |
| 995 | **−?** (board ObjectSetup in frame 0, no scrub; "starts" logged at 13563) | 1 |

After the fix, the hashed state at relative frames 0–3 (`sys`, `plr0`, `cfg`, `heap`, `ovl`) of the 993/994/995/1911
runs is identical to the 1500 control, heap layout included (`heaps BDDB26B214B20C84` at every start). Only `pad`
differs, because the script's pads are tied to absolute frames. Before the fix, 994 was one frame ahead and 995 two.
Before the sprite-group kill, 995's heap was `9D455BAF…` (SYSTEM 8 blocks vs 7).

### Two peers: a rejoin before turn 1 (`harness/early.sh <tag> <host-frame>`)

The setup: a private netns, relay off, and N38/N57's in-game button presser plus pad masher (`harness/instrument.diff`
and `harness/n67_harness.inc`, applied for the runs and reverted). Once the host's hash log reached `<host-frame>`
in the board's intro, the guest was SIGKILLed. A fresh guest process then used Online > Join > Rejoin Match.
Before turn 1 there is no turn point, so the host reloads from the match start: it is inside w01 (`ran 3/4 board
process destructors`, `killed 0 menu processes`), and the guest is in modeseldll (`killed 2 menu processes`).

| run | code | guest killed at | board start, host / guest | epoch 1 |
|---|---|---|---|---|
| e1 | before the fix | 600 | frame 1 / frame 1 | identical for 7367 frames (to the end of the log) |
| e2 | fix | 300 | frame 1 / frame 1 | identical for 8654 frames: rest of the intro, turn 1 players 0–3, instDll, m-overlay 40 |
| e3 | fix | 1000 (just before the turn-order dice) | frame 1 / frame 1 | identical for 8177 frames, through the turn-1 minigame (ovl 30) |

In all three, both peers' board start had the fresh layout `3E1CFDACE4558E3D`, and no desync was logged.

### Other

- `tools/netplay/match_test.c`: the hold is FALSE before `MatchStartProc`, TRUE right after it in the same frame,
  FALSE a frame later, and FALSE once the board has started. Mutation: if `MatchStartProc` does not set the switch,
  the test fails (`match_test.c:338`).
- Offline unchanged: `--script mash-a.script --turbo` (no match, netplay active as in every `--script` run) gives a
  hash log identical to the pre-fix build's for all 17400 frames (boot, party setup, w01, minigame, results, w01).
- `tools/check.sh` passes.

## Tried / failed

- First fix attempt: restart omwatchproc's loop at both points, with nothing else. The frames were right, but 995's
  heap had modeseldll's `espInit` sprite group left over (above). Fixed with the group kill.
- I did not take the "hold the lockstep until every peer's board starts" route. That would need a barrier in pbnet,
  and the scheduler already gives a fixed start frame once the in-flight case is handled.
- My scratch `sed` that undid a mutation also flipped the `match.switched = FALSE` in `netplay_match_start`. I caught
  it in the diff and fixed it before the gate. The in-game runs were made before the mutation, on the correct code.

## Follow-up

- N76: a reload while a peer is paused (below).

## Not verified

- **The in-flight case with two peers.** The two-peer harness cannot time a lobby start to land in a guest's
  one-frame overlay switch, because the start comes over the network. The mechanism and the fix were checked in one
  process with `--match` (above), which goes through the same `netplay_match_tick`/`MatchStartProc` path.
- **A peer whose overlay's ObjectSetup itself sleeps when the start applies** (omwatchproc inside `omDLLStart` with
  `omcurovl` already set). By reading the code, the kill then comes from `omOvlKill` and the start is still frame 1.
  But the old ObjectSetup goes on running after its data is freed. I found no menu that sleeps in its ObjectSetup,
  and did not run this case.
- **A peer that is paused (START) when a reload comes.** Pause flags do not change the start frame (omwatchproc is
  exempt from pauses), but `MatchStartProc` does not lift `Hu3DPauseSet`/`HuSprPauseSet` the way N34's leave does.
  Not run. That is a state question, not part of N67's start-frame question.
- The bootDll (title) path of a fresh rejoining guest (skipBootSequence off), more than two peers, other boards,
  other platforms: not run. N41/N64 covered a match start from the title in one process.
