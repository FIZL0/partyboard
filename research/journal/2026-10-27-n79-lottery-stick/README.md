# N79: a stick input for the board lottery, so party_check.sh reaches `match over`

## The problem

Since N69, `tools/netplay/party.script` (w01, seed 0x50424E54) has player 0, a human tapping A, buy a lottery ticket
in turn 8, and the party never gets past it. lottery.c has two games, and both wait on the stick of a human current
player (`GWPlayer[player_curr].port`):

- **Ball game** (`ExecBallGame`, window `CreateLotteryWin(0)`): the handle only turns while `HuPadStkX/Y` turns clockwise
  (the angle is `atan2d(x, y)`; a step counts if it rises, a step over 30 degrees counts as 10). At least one axis must
  be >= 57.6 after `PADClamp` (main stick max 72), and `x` or `y` == 0x7F stops it at once (COM end marker).
- **Ticket scratch** (`ExecScratchTicket` → `TicketUpdate`, window `CreateLotteryWin(1)`): the hand comes down for 60
  frames to (320, 240), then moves 8 px a frame in the stick's direction (screen y down = stick y negative) inside
  x 192..384, y 192..288, and clears a 17x17 brush of the ticket bitmap. It ends when all but 300 pixels are clear.
  N69's circling stick couldn't do this: a circle at 8 px/frame stays on one ring and never clears the whole ticket.

The ticket pick before the scratch (`CreateLotteryWin(2)`) reads the d-pad and A, and the script's A taps handle it.

## What changed

- `src/game/board/lottery.c` (TARGET_PC only): `lotteryInstWinKind`, the `CreateLotteryWin` argument while
  `lotteryInstWin` is up (set there, -1 in `KillLotteryWin` and `BoardLotteryNetplayReset`), and
  `BoardLotteryNetplayStickGame()`: 1 ball game, 2 scratch, 0 neither. The window shows up at the same time as each
  game's stick loop (for the scratch it also covers the hand's descent and rise), so no game logic changed. Offline
  play only gets two stores into a byte that nothing reads.
- `src/port/netplay.c`: a script command `lottery-stick on`. In `script_pads`, while the stick game is up and the
  current player is a human, that player's port gets `lottery_stick_at(game, t)`, where t is the frame count since the
  game came up:
  - ball: an 18-step table, 20 degrees a frame clockwise, radius 120 (under 0x7F);
  - scratch: 64 frames idle (the descent), left 30 and up 15 frames to the top left corner, then 13 rows of
    26 frames across plus 1 frame down (8 px), and again from the corner until the game ends.
  It prints `netplay: script: lottery <game> at frame F (player P)` when a game starts.
- `tools/netplay/party.py` / `party.script`: `lottery-stick on`. `tools/netplay/tick_gate_test.c` stubs
  `BoardLotteryNetplayStickGame` (it links netplay.c without the board; the first `done` failed on it).

## Runs (GMPE01, `tools/netplay/party_check.sh`, --turbo)

| run | result |
|---|---|
| pair 1 (before the log line) | both reach `match over` (110988 and 116948); **hashdiff: first difference at 78195**, in m440 (ovl 48, from 76339): `frand` and `pad` (COM input written to the pads) differ, `sys`/`plr*` the same. Identical through the lottery and the turn after it. |
| pair 2 | **identical 135000 frames**, both `match over` at 116948, exit 0. Lottery games: scratch at 39532 (player 3, COM), ball game at 55784 (player 2, COM), **scratch at 69687 (player 0, human: the script's stick)**, scratch at 93377 (player 1, COM). 15 minigames. |

The m440 divergence in pair 1 happened once in two pairs, after the lottery, in a minigame the party never reached
before N79. Filed as **N83**. Candidates already in the journals: N73's m440 main.c:1441/1775 loop UB and N69's
object.c:1152 `var_r28` maybe-uninitialized. I didn't look into it here: it's outside this task.

## Tried and dropped

- Working out the current game from the lottery's existing statics (`turnMot`, `ticketSprGrp`, `lotteryMdl[4]`, `currPrize`) instead of
  adding a byte: `ticketSprGrp` stays set until `DestroyLottery` (the board's end), and `lotteryMdl[]` starts at 0
  rather than -1, so a later lottery visit would be detected wrong.
- One stick for the whole party: it would change the board walk and every minigame.

## Not verified

- **The ball game with a human player**: only COM player 2 played it in this party (the hook leaves COMs alone), so the
  spin table hasn't been run for a human. It comes from reading `ExecBallGame` only.
- **How many frames the scratch took**: there's no end-of-game log line. The hand's coverage (row step 8 hand px vs a
  17 px brush) assumes the ticket bitmap's height is <= 204 px. I didn't read its size, but the human scratch ended,
  so it held here.
- **m440's divergence (N83)**: seen once, not reproduced, not looked at. The pair-1 summary and hash-log lines
  78185..78200 of both runs are in this directory.
- **Two lockstep peers / Windows / Android / Apple**: not run. The command is --script only.
