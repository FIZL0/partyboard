# N56: the two-peer lobby desync at frame 1080 (BoardRand) on w01

N47's slot saw every lobby match desync at frame 1080 on w01. The first differing section was `brand` (BoardRand)
at frame ~1077, and the desync came back in every epoch, so N46 resynced from the match start forever. I found two
bugs that each give this pattern and fixed both. After that, N47's two-peer kill/Continue check passes.

## What was wrong

### 1. A `--hash-log` peer's match could start one or two lockstep frames late (src/port/netplay.c)

`--hash-log` (and `--script`) call `netplay_begin` at boot (`netplay_debug_init`), so netplay is active **in the
menus** of every harness peer. When the host presses Start Match, pbnet's lockstep for the lobby goes active on the
lobby thread. `netplay_lobby_poll` applies the start from `ui_update`, at the end of a loop. Until it does,
`netplay_tick_begin` stepped the lockstep in the menu: pbnet frame 0 (sometimes 1 too) ran with the menu state. Then
`netplay_match_start` → `netplay_begin` set the game's frame to 0 again. So a peer's match frame 0 was pbnet frame 0,
1 or 2, depending on how the lobby thread's start fell against that peer's loop.

Evidence (scratch log of every lockstep hash, `harness/instrument.diff`):
- In fresh runs, the lockstep hash at pbnet frame 0 was `46377BCF8025A0EE` on every peer for every seed. That is
  the menu state hashed before the start. N47's desync logs have the same frame-0 hash on both peers.
  `n56: hash f30 (frame 29)` means pbnet frame 30 hashed game frame 29.
- A guest whose netplay had ended (it left an earlier match, so it was not active in the menu) hashed frame 0
  *after* the start (`hash f0 (frame 0)`). The `--hash-log` host hashed the menu (`hash f0 (frame 425)`). This
  desynced at frame 0, three runs out of three (`rejoin.sh`, runs r1–r3).

Why a one-frame shift shows up at 1080 and not earlier: the w01 intro (logo, camera, the players' fall) is timed,
and its hashed state (`GWSystem`, `GWPlayer`, seeds, pads) does not change. So the 30-frame hashes match one frame
apart. Then Toad's two messages (`ExecStartRoll`, `BoardWinWait`) wait for an A edge. The harness taps A for frames
`f % 24 < 4`. A message that gets ready one frame later can miss a tap and wait 24 more frames. Two messages give
~48 frames. In N47's logs the guest's turn-order dice started at ~1077. In every run here they start at 1126.
1126 − 48 ≈ 1077. The dice draw BoardRand every frame (`PlayerDiceRoll`), so `brand` is the first section to
differ. N47 hit the race and N38's runs did not. In all my fresh runs both peers stepped exactly one menu frame, so
they agreed with each other.

Fix: `netplay_tick_begin` does not step the lockstep unless `netplay_match_running()`. The started lobby's lockstep
waits until this peer has applied the start, so its frame 0 is the match's frame 0 everywhere. For players nothing
changes: without the debug flags netplay is only active from `netplay_match_start` on, so this branch was already
never reached in a menu.
`tools/netplay/tick_gate_test.c` checks it: a session with the lockstep on and no match running does not step,
take pads or hash. I compiled the test against netplay.c without the gate, and that assertion fails.

### 2. `playerOrderNew` survives from the last board start (src/game/board/start.c)

With fix 1, a guest that had already played a match desynced in its second match at **frame 1170, in every epoch**
(3 runs out of 4, `rejoin.sh`). The BoardRand values matched draw for draw up to 1162. Then the caller of one draw
differed: on the host it was `PlayerDiceRoll`'s redraw loop after a jump (start.c:530), and on the guest it was a
CPU's jump check (start.c:507). From that frame the CPU seat 3 jumped on the guest and not on the host (scratch
per-frame motion log). The cause is `static s8 playerOrderNew[4]`. It is never reset. A fresh process has zeros,
and a process that already ran a board start has that start's rolls. The redraw loop
`while (playerOrderNew[player] == data->digit && player != ...) data->digit = BoardRandMod(9)` compares the first
jumpers' digits with those stale rolls, so the number of BoardRand draws differs. After a desync each peer keeps its
own rolls from the failed epoch, so every resync from the match start desyncs again. That is the "every epoch" in
the N56 report. N47's guest left matches and joined new ones (`cmd-leave`/`cmd-join`), and its reloads ran board
starts again.

Fix: `CreatePlayerStart` clears `playerOrderNew` when `netplay_active()`. Every peer then rolls as a fresh process
does. Offline is unchanged: a second party in one process keeps the decomp's (and the GameCube's) behaviour.

## Checks (GMPE01, `$PB_DISC`, relwithdebinfo, Linux)

Both peers ran in a private network namespace with the relay off (N38's way). Each peer had its own user dir under
`.n56-runs/` (deleted afterwards), vsync off, skipBootSequence on (off in t4/t5), and `--hash-log`. A scratch
harness pressed the Online menu's buttons (`harness/`, see below).

| run | what | before the fixes | after |
|---|---|---|---|
| t1–t5, b1–b4 | fresh host + fresh guest, to frame 1500–5700 (t3 pinned both peers to one CPU, t4/t5 from the boot sequence, b1–b4 four pairs at once) | no desync. Dice at 1126 on every peer | – |
| t6, t7 | `--desync-at` on the guest: resync from the match start | epoch 1 identical | – |
| r1–r3 | match 1, both leave at 1500 (`--leave-at`), the host is killed, a fresh host B hosts match 2, and the guest joins it | desync at frame 0 and frame 30 (fix 1), epoch 2 heals | – |
| r4, r6, r7 / r5 | the same with fix 1 only | desync at 1170 in every epoch (fix 2) / r5 clean | – |
| r8, r9, r10 | the same with both fixes | – | no desync. Match 2's hash logs identical from frame 0 for all 1500 frames, three runs |
| k1 | **N47's kill/Continue**: lobby match, host SIGKILLed after player 2's turn point (frame 4242), guest presses Leave Match, host relaunched in the same user dir, Continue tab > Toad's Midway Madness > Host Match, guest joins the new code | – | both log "the board resumes at player 2's turn". `hashdiff.py`: **identical for 6399 frames** from the resume (players 2 and 3, the minigame, turn 2 players 0–1). Both online saves byte-equal at the end |
| k2 | the same, host killed after player 1's turn point (frame 3116) | – | resumes at player 1 on both. **Identical for 7574 frames** (overlays 89, instDll 3, minigame 13, results 84, back to 89). Saves byte-equal |

Also checked:
- The first match before the kill in k1/k2 was identical between the peers until the kill (4311 and 3147 frames).
- At the same turn, the two peers' online saves were byte-equal (t1, t2, t3, k1, k2). N47 had listed this as not
  verified.
- `tools/check.sh` passes, and `tick_gate_test` has the new check.

## How it was run (harness/, scratch, not committed in src/)

- `instrument.diff`, applied for the runs and reverted:
  - N38's `n38_pads`: this machine's seat taps A every 24 frames, and START in instDll/results.
  - N38/N47's in-game button presser, included into `online.cpp`. `n56_harness.inc` is N47's with two
    changes: after a match ends it leaves the lobby and hosts or joins again, and the guest deletes the code file
    once it has read it.
  - With `PB_N56` set:
    - a BoardRand trace: frame, seed and the callers through `backtrace` + `dladdr`. Run `addr2line -f -e libdol.so`
      on the offsets.
    - every lockstep hash for frames < 100, with seeds.
    - the four player models' Hu3D motion state for frames 1080–1175.
- `pair.sh <tag> <secs> [vsync]`: a fresh host + guest in a new netns (`GARGS` go to the guest, `PFX` before both,
  e.g. `taskset`).
- `rejoin.sh <tag>`: match 1 with host A and the guest, both `--leave-at 1500`, then a fresh host B hosts match 2
  and the guest joins it.
- `killcont.sh <tag>` (`KILL_PLAYER=n`): N47's kill/Continue.
- Split a `--hash-log` into sessions with N38's `harness/split.py`, then compare them with
  `tools/netplay/hashdiff.py`.

## Tried / failed

- Five fresh pairs, one pinned to a single CPU, two from the boot sequence, and four pairs at once: none desynced.
  The race in fix 1 needs one peer to step a different number of menu frames, and this machine never did that with
  two fresh `--hash-log` peers. I found the mechanism with a peer that was *not* active in the menu (a guest after
  `netplay_end`).
- Theories I ruled out along the way:
  - game logic polling audio state: msm is stubbed, so the status calls return 0
  - async DVD/ARAM: both are synchronous in the port
  - VI retrace counts: the stub returns 0
  - WAIT-frame draws: the BoardRand trace showed every draw inside a tick
  - port settings that could differ between peers: none reach board logic
- The first motion log used `dlsym("BoardPlayerModelGet")`, which is a static inline and is not exported. I read
  `boardPlayerMdl` instead.

## Not verified

- **N47's exact desync logs reproduced as such.** I did not get a fresh-pair run where one peer stepped two menu
  frames and the other one, so the 1077-vs-1126 explanation is from the mechanism and the numbers, not from a
  rerun of that race. The r1–r3 runs show the same mechanism with 0 vs 1 menu frames, and the fix removes every
  menu step.
- **A reload from the match start while the peers are in different overlays** (one peer in the board, one in a
  menu). r1–r3's epoch 1 desynced at frame 30 that way, because the guest's board overlay started a frame earlier.
  That came from fix 1's bug, and with the fixes no run reached that case. A rejoin before the first turn point
  could still reach it (N38 did not run it either). Filed as **N62**.
- **Other statics carried across board sessions.** I fixed only the one that desynced. No audit of other board or
  overlay statics for "read before written in a new session". Filed as **N63**.
- Killing the guest instead of the host (the harness has no role that rejoins as host after a kill), more than two
  peers, other boards, real networks and other platforms: not run.
- Offline: `playerOrderNew` is only cleared under `netplay_active()`, and the tick gate is only reached with netplay
  active. No offline run was made for this task.
