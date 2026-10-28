# N104: the browser pair's fake pad plays the stick-only waits (board lottery, m444 plunger)

## Problem

`tools/web_pair.mjs`'s fake standard gamepad only tapped A and START. A human seat that reaches a wait which reads
only the stick then stays there for ever, and the run still PASSes (N97 run 2: w01 turn 4, Luigi at the lottery's
"Scratch Off" from frame ~28.7k to 109.7k). Scripted desktop runs had the same hang and got `lottery-stick on` (N79)
and `plunger-stick on` (N114) in `src/port/netplay.c`.

## What changed

- `src/port/web/web_stick_probe.c` (web build only, `cmake/web.cmake` adds it and exports both functions):
  - `pb_web_lottery_stick()`: `BoardLotteryNetplayStickGame()` (N79's, `src/game/board/lottery.c`: 1 ball spin,
    2 ticket scratch) when the current player is human, else 0. No change to `lottery.c` was needed: N79's
    export already says which game runs.
  - `pb_web_plunger_pad()`: the pad m444's plunger loop waits on, or -1. That loop already calls
    `netplay_script_plunger_wait(pad)` every frame while netplay is active (N114). That call now also records the
    frame and pad (whether or not a script runs), and the new `netplay_plunger_waiting()` (`include/port/netplay.h`)
    returns the pad if the wait was seen in this frame or the last one. It changes nothing in the game's state or
    pads, so the hashes and offline play are unchanged.
- `tools/web_pair.mjs`, in the page's fake pad (the 8 ms interval):
  - Lottery: while `pb_web_lottery_stick()` is nonzero, the axes follow netplay.c's `lottery_stick_at` exactly. The
    pattern is indexed by game frame (`pbnet_frame()` minus the frame the game was first seen), not by wall-clock
    time. The game reads the pad once a frame, so each frame gets the next step at any frame rate. The ball spins
    20° a frame (under the 30° limit); the scratch waits out the hand's 64-frame descent, then rasters. A new game, or
    a frame count that starts over (a resync reload), restarts the pattern. Axes are `x / 127`, `-y / 127` (the
    standard gamepad's y points down, the GC stick's points up).
  - Plunger: while `pb_web_plunger_pad() >= 0`: stick down and A held back for its first 90 frames (a full pull,
    as N114's `PLUNGER_PULL_FRAMES`), then the usual A taps shoot.
  - Every stick game a tab's pad starts is listed in `window.__pbStick`. The run log prints new entries every 10 s
    (`a: stick game: scratch at frame N`) and all of them at the end (`a: stick games: ...`).
  - Each tab moves its stick whenever the current player is human, even when that seat is another tab's. Only
    the current player's port is read in these waits, so the other tabs' sticks do nothing. The harness doesn't
    know which seat is local (netplay keeps `local_seats` only while parsing the lobby), and that didn't seem worth
    adding.
- The JS pattern was checked against the C one: `lottery_stick_at` copied into a scratch C program and the page's
  `lotteryStick` run in node with a fake `pbModule`, 1200 frames of each game. Same values.

## Runs

Build: `WEB_FAST=1 tools/web.sh`. Before it built, `git submodule update --init extern/musyx` was needed (this
worktree's musyx was at an old commit with no `musyx/pc.h`. check.sh does that update, web.sh doesn't).

- **Try 1** (lottery only, seeds 0x5104A0001 / 0x5104A0002, `WEB_PORT` 8932/8934, both pairs at once, ~60 fps):
  neither got to a human lottery game that we could see. Both stood in **m444** (overlay 52, the board-event
  pinball) with a human seat at the plunger ("L↓ Adjust Power / A Shoot"): run 1 from turn 3 (frame ~37k), run 2
  from turn 4 (frame ~31.5k). Frames kept going but nothing moved on. That's the same kind of stick-only hang, so
  the plunger went into this task too. Both were stopped at ~820 s.
- **Try 2, run 1** (lottery + plunger, seed …01): **m444's plunger was played**: `a: stick game: plunger (pad 0)` at
  frames 23462, 24167 and 24958 (three shots), then overlay 89 (back on the board) and the next turn point (frame
  26350). Then from turn 6 (frame ~53.5k) it stood at **instDll** for a board event's mid-turn minigame. No
  `resync point ... minigame` line comes before that instDll, so the harness never pressed START.
- **Try 2, run 2** (seed …02): the same instDll stall at turn 3 (frame ~23.4k, "Paths of Peril", screenshot: Start
  / Practice / Game Rules). That's not a stick wait, but it stops a long run in the same way and the fix is small,
  so it went in too: `Start New OVL 3 ` (instDll itself) now arms the START. Its frame is the one the main loop sees
  next (`mg.at = -2`), and a minigame point that follows (same frame, the usual case) sets its exact frame.
- **Run 3** (fixed harness, seed …03): turn 8 (frame ~100k). Then it stood in **m432** (overlay 40, minigame 31):
  both human seats at the bottom of the climb, 3'42" on a clock with no limit. A taps can't play it. That's
  outside this task. Filed **N125** (minigames A taps can't finish). Run 4 got through the same m432 at turn 3,
  so it doesn't always hang.
- **Run 4** (seed …01 again; a different match, since pad timing is wall-clock): **a human lottery ball game** at
  frame 75792 (turn 8, player 1). It ended and turn 8 went on to player 2 at frame 78158. ExecBallGame's loop ends
  only once the ball's motion reaches its end, and its speed grows only from stick turns (an idle stick decays it
  to 0), so the fake pad's spin is what finished it. Then turn 9's minigame 26 (overlay 35, the 2v2 boat race)
  stood with both human boats at the start (N125 again; run 3 got through the same minigame at turn 2).
- **Run 5** (`PAIR_TABS=4`, four human seats, seed …05, ~15 fps a tab): **the instDll fix fired**: turn 2's
  instDll came with no minigame point, then overlay 46 started (`o3 o46 o84 o89`).
  The plunger was played there too (pad 3, seat d's, frames 38643/39436/40213, then overlay 89). From turn 6's
  minigame 27 (overlay 36, frame 78367) it didn't move on (N125's class; run 6 got through the same overlay). It
  ran past my `timeout -s KILL 3300` (four tabs make the harness's loop slower than the clock), so it has no summary,
  and its Chromium was left behind (spawned detached). I killed it by the pid listening on its DevToolsActivePort,
  as its own process group.
- **Run 6** (seed …06): **a human ball game** at frame 45930 (turn 4) and **a human ticket scratch** at frame 96327
  (turn 9, Luigi at the lottery shop in the shot just before). Both ended, and the next player's turn point came at
  frame 97645, 1318 frames after the scratch began. ExecScratchTicket's loop has no timeout: it ends only when
  the ticket counts as scratched (`ScratchTicketCheckDone`) or TicketUpdate says so. In N97 run 2 a human
  scratch stood for 80k frames, so the raster is what finished it. Its mid-turn instDll at turn 4 (overlay 47)
  also got its START.

  Run 6's summary: `reached: a: turn 17, 17 minigames started, last turn/minigame point at frame 173816, menu
  processes killed 1` (b the same), no desync, frames 174309, **PASS**. Still moving when the 3000 s ran out
  (turn 17's point came 500 frames before the end): the furthest a browser pair has got (N97's was turn 4).

So with the fake pad's stick, all three stick-only waits were seen to finish in the browser pair: the ball game
(runs 4 and 6), the ticket scratch (run 6) and m444's plunger (try 2 run 1, run 5). The runs that still stopped
short of the party's end stopped in minigames that A taps can't play (N125).

## Checks

- `tools/check.sh` passes (run again after the `netplay.c` change).
- `node --check tools/web_pair.mjs`.
- The desktop build compiles `netplay.c`'s new function. The web build (`WEB_FAST=1 tools/web.sh`) exports
  `_pb_web_lottery_stick` and `_pb_web_plunger_pad` (in `partyboard.js`).

## Not verified

- **A whole party (turn 20 and the results) in the browser pair**: not seen. Run 6 got to turn 17 with nothing
  stuck, but at ~55 game frames a second 3000 s isn't enough for 20 turns. ~3600 s plus a seed that misses N125's
  minigames would do it. Runs 1-2 stopped at the mid-turn instDll (now fixed). Runs 3, 4 and 5 stopped in
  minigames that need the stick (N125).
- **Offline hashes / a desktop `--match` run** after the `netplay.c` change: not run. The change only stores the
  frame and pad on each `netplay_script_plunger_wait` call and adds a reader. No game state, pad or hash input is
  touched, and the call only happens while `netplay_active()`.
- **PAIR_MENU runs** (the lobby through the Online menu): not run. The fake pad is the same in both modes.
- **The scratch's raster on screen**: no screenshot fell inside the 1318 frames of run 6's scratch (shots are 30 s
  apart). That it ended is the evidence.
