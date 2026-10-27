# N64: desync check covers gameplay only (and why N42's peers ran a frame apart)

**Outcome: done.** N42's run (the host continues a turn-10/10 online save through the Continue tab, the guest joins,
2 humans + 2 CPUs) now reaches `match over` on both peers at the same frame with the same hash and no resync. Each
peer then plays the party results by itself and is back in the lobby with the toast. A second Start then plays a fresh
party's turn 1, minigame and results with **identical** `--hash-log`s on both peers, in every column (the seeds and
the heap layout too).

The user's direction came mid-task (2026-10-27, "we shouldnt worry about a few frames on a menu or the dust particles,
as long as the gameplay is the same"). I stopped chasing the motion offset itself and made the check gameplay-only.
The rerun then showed that the same per-peer offset **does** reach gameplay: in a fresh party's board intro, the
turn-order dice roll `BoardRandMod` on every frame they spin. A probe then found the cause (below), and it is fixed.

## What changed

1. **Gameplay-only hash** (`src/port/netplay.c`). `netplay_hash` = GWSystem, GWPlayer[4], GWPlayerCfg[4] and the pads.
   The frand/rand8/BoardRand seeds are no longer in it: effects roll them too (UpdateNpcDust), and what a gameplay roll
   decides lands in GWPlayer/GWSystem anyway. The old hash is `netplay_full_hash`, written as the `--hash-log` column
   `full=`. There is also a new column `gp=` (1 while the lockstep sends hashes). `hashdiff.py` compares every column, as
   before.
2. **No desync outside gameplay.** `pbnet_set_hash` is only called while `omcurovl` is a board (w01..w06) or a
   minigame (m300..m463) overlay (`netplay_gameplay_ovl`). Menus, instDll, resultDll and the party results send no
   hash. pbnet compares only the frames both peers hashed, and keeps the other peer's unmatched hashes in a bounded map
   (`THEIRS_KEPT`), so a peer that hashes one frame the other did not is harmless.
3. **Reseed at gameplay points** (`netplay_point_reseed`): frand, rand8 and BoardRand get
   `mix(session seed, salt, point, GWSystem.turn, GWSystem.player_curr, overlay)`. The frame is not an input. The points:
   - every player's turn start (`netplay_turn_checkpoint`, `src/port/netplay_resync.c`; MainFunc already calls it)
   - every minigame overlay start, and resultDll's start (`netplay_ovl_start`, `src/port/netplay_match.c`)
   A resync reload reaches the same turn start, so it gets the same seeds.
4. **The match ends where the results start** (`netplay_match.c`). The party's results (mstory3dll after the board) are
   now the end, the same frame on every peer. The board's own exit to modeseldll still ends it too. Before, the end was
   modeseldll after the results. That made N25's end depend on the results playing out alike, which they did not (N42
   m7: the guest left at 5420, the host stayed, and then WAITed for good). After the end, the results run outside the
   session:
   - every human seat's `GWPlayerCfg.pad_idx` becomes 0, because the results wait for each human's A on that seat's pad
     (result_seq.c `fn_1_1442C`). Without this, the guest's results waited forever for seat 1's pad (run p2).
   - the canned GWGameStat stays until the menu after the results (`netplay_match_results`). objmain's guard is
     `netplay_active() || netplay_match_results()`. mstory3 writes records into GWGameStat, and as before they don't
     reach the player's own.
   - the lobby UI waits for the menu (`netplay_lobby_returned`), and `netplay_party_finish` does not delete the
     offline autosave for an online match's results.
5. **The root cause of the per-peer offset** (`src/port/netplay_match.c` `MatchStartProc`, `src/game/window.c`
   `HuWinProcActive`). bootDll's normal path runs `CharInit`, `HuWindowInit`, `HuWinInit(1)` before the title, and sets
   `SystemInitF` only after it. A peer whose match starts during the title (the N42/N64 guest: it joins about 270
   frames after boot, and it is the only one that logs `system init done for bootDll`) ran N41's init again.
   `HuWindowInit` sets `winProc = 0` **without ending the running window process**, so the board's `HuWinInit` started a
   second `HuWinProc`. It is dol code, so no overlay change ends it. It runs for the rest of the process, on that peer
   only. Every window then updates twice per frame, so a message ends a frame earlier. That moves whatever waits for it:
   - the 15-frame `Hu3DMotionShiftSet` after a message in mstory3 (`fn_1_13C78`), so UpdateNpcDust's frand (N42)
   - the board intro's dice spin, so BoardRand and the turn order (run p3, below)

   Now `MatchStartProc` calls `HuWinAllKill` first when a window process runs. Side effect: the heap layouts of the two
   peers at board start are now **equal** (`00972594A04DB83D` on both in p5; N26/N30/N42 saw them differ from frame 0).
6. `--desync-at` (N46's debug flag) also flips player 0's coins: a frand flip alone is no longer in the lockstep hash.

Tests:
- `tick_gate_test.c`: the hash ignores the seeds, the full hash doesn't; no hash in mstory3, a hash in a minigame; the
  reseed is the same for the same point at another frame and with other seeds before; the lobby waits for the results.
- `match_test.c`: the reseed points; the end at mstory3 (pads, canned GWGameStat until modeseldll, once); the window
  process killed before re-init.
- `autosave_test.c`: the turn reseed; no autosave deletion during an online match's results.

## How it was run

- **A turn-10/10 save in 10 s** instead of N42's ~40 min: `run.sh save` with N30's `save-continue.py save` script
  (`end 9200`) writes the post-minigame turn-1 checkpoint (frame 9181). `harness/last_turn.py` then sets
  `GWSystem.turn` (blob byte 4) to `max_turn` (byte 5). Continued, the board runs ~12 frames and goes to the results
  (ovl 78). That is the same path as N42's real turn-10 save, and it reproduces N42's desync (run p1: desync at 210
  in every epoch, the same two hashes).
- `harness/pair.sh <tag> <save> [secs]`: N42's netns + Continue-tab host + Join guest (the host's code goes into the
  guest's online.json). It needs N42's menu driver `harness/n42_harness.inc`, included in `online.cpp` as in N42, and
  the scratch code in `harness/netplay_scratch.c` and `harness/scratch.diff`:
  - `n42_pads`: the lockstep pads
  - `n64_local_pads` + the pad.c hook: pads for the local results after the session
  - `n64_probe`: a per-frame dump of the process list (func module+offset, prio, stat, sleep) and of every model's
    motion/shift/time. `harness/pdiff.py <host session> <guest session> <n>` diffs the two dumps by session.

  All scratch code is reverted; none is committed in `src/`.

| run | build | result |
|---|---|---|
| p1 | base + harness | desync at 210, every epoch (N42's) |
| p2 | gameplay hash + end at results | both `match over at frame 15, hash 4D8A…`, no desync; both stuck in the results (seat 1's pad) |
| p3 | + pad_idx 0 | both reach modeseldll + lobby toast; second Start: **desync at 1110** in the board intro (boardRand at 1078, plr0/plr1 at 1083), the same in every epoch |
| p4 | + probe | first difference: an extra `HuWinProc` (window.c:567) on the guest, at every frame |
| p5 | + window fix | match 1: frame 15, `EE0A…` on both; match 2: identical logs for 8786 frames (turn 1, minigame 25, resultDll), no desync |
| p6 | final code + harness, guest `--desync-at 2400` | desync at 2400 caught, one resync, then identical for 9101 frames (minigame 20 and its results) |

Single-process checks with the final build:
- `--continue` of the turn-1 save by two menu paths (title vs mgmode with skipBootSequence): `hashdiff --from 900`
  **identical: 19100 frames**.
- The turn-10 continue logs `match over: the results (overlay 78) start at frame 914` and the scripted session goes on.

## Tried and dropped

- Before the change of direction: filling fresh heap blocks and coroutine stacks with patterns (00/FF on stacks, 00/99
  on heap blocks). Four single-process runs were identical, so the cause was not uninitialised memory. The menu path
  before the match (title vs mgmode) made no difference either.
- The first `ovl >= DLL_m300dll` minigame test also matched the boards (their ids are higher); match_test caught it.

## Not verified

- **N42's other items** (the host's new room id, nobody ready, N31 on two peers): N42 is to rerun them (it is blocked
  `--after N64`). Seen in the logs of run p5: after their results, both peers showed
  `[The match is over. Back in the lobby.]` (RmlUi text). The guest pressed Ready, the host pressed Start Match again,
  and both entered the board.
- **A whole fresh party on two peers**: runs went to turn 1's results (about 9000 frames), not 10 turns. Other
  per-peer differences later in a party are not ruled out (N53).
- **Minigame state** (scores inside a minigame) is not in the hash. A divergence there shows only when it reaches
  GWPlayer, at the board after resultDll (resultDll itself sends no hash). This is by design (gameplay = GW state),
  but it is slower to notice.
- **Cosmetic BoardRand rolls inside a turn** still reach that turn's later rolls. The reseed bounds them to one turn.
  With the window fix no such difference was seen.
- **Real machines / real pads**: one host, one netns, synthetic button events, as in N38/N42.
- **Offline play**: the guards are netplay-only (`netplay_active()`, or `netplay_match_results()`, which only an online
  match sets). `HuWinProcActive` is a new accessor that offline code never calls. No offline party was played through.
