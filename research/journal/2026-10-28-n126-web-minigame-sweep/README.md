# N126: web_pair minigames that A taps can't finish

## Problem (from N104)

N104 runs 3 and 4 of `tools/web_pair.mjs` looked hung in two minigames where the human seats stood still and only A
was tapped: m432 (overlay 40, minigame 31) at "3'42" and counting", and m427 (overlay 35, minigame 26, "Right Oar
Left", the 2v2 boat race) at "2'28" and counting".

## Finding: neither is endless, both stop at their own 5-minute cap

- **m432** (`src/REL/m432Dll/main.c`): `MGSeqCreate(1, (300 * REFRESH_RATE + REFRESH_RATE - 1 - lbl_1_bss_57C) / REFRESH_RATE)`
  starts a countdown at 270 s and the game ends at 300 s. Seen in run m432-1: a "14" countdown on screen at 4'46"
  (`a-match-420.png`), and both forced m432 rounds lasted 313 s from overlay start to `Start New OVL 84` (the result).
- **m427** (`src/REL/m427Dll/main.c` ~242/297): the same, `lbl_1_bss_80 == REFRESH_RATE * 270` starts the countdown
  and `>= REFRESH_RATE * 300 + 1` ends the race. Run m427-idle-sweep: 316 s.

N104 stopped watching before the 5 minutes were up. So a long run still reaches the party's end. Each such minigame
just costs up to ~5 minutes (about 18k frames at 60 fps) instead of about 1.

A rough survey (`grep` for `MGSeqCreate(1, ...` and `REFRESH_RATE * 270/300`) also finds 270/300 s caps in m448, m449
and m457. Timers are set up too many different ways (hex constants, work fields) for a grep to say which minigames
have **no** cap. I didn't read them all one by one.

## What the minigames want (from the code)

- **m427**: a boat holds two players. Each player's A (`unk_44 & 0x100`) rows the left oar (`unk_58`) and B (`0x200`)
  the right one (`unk_5C`); turning is the difference. A alone turns the boat, so with A taps only, a boat whose
  partner is a CPU (which steers toward the next waypoint) still gets out. A boat with two A-only humans does not.
- **m432**: a co-op climb per team. Players walk forward (CPU: stick y `0x48`, A jumps). Stations along the lane are
  claimed by proximity, only on the ground (`fn_1_C33C`: y ≤ 20 for the 0x700 kinds). A 0x100 station counts B presses
  and goes up to 25; a 0x200/0x400 station counts Y/X presses. Both drop the player after 20 frames without one. A
  0x800 crank counts an R pull followed by an L pull (`fn_1_D9EC`: one trigger at a time).

## What changed (tools/web_pair.mjs)

- **Minigame sweep** in the fake pad. The harness calls `window.__pbMinigame(overlay)` at every `Start New OVL`
  (0 when it isn't a minigame, i.e. outside 4..69). Once a minigame has run 2400 game frames (`pbnet_frame`, so it
  doesn't depend on the tab's frame rate; `PAIR_SWEEP=N` sets another wait, `0` turns it off), the pad plays three
  480-frame phases, over again:
  forward (stick up, A and B in turn, a press every 4 frames), no jump (stick up, B/X/Y in turn, no A), and around
  (the 8 stick directions, 60 frames each, A/B/X/Y in turn). L and R also take turns, 20 frames each, all along.
  The A taps, START (instDll) and the plunger come first, as before. A frame count that starts over (a resync reload)
  starts the wait over. Every sweep is listed with the N104 stick games (`a: stick game: minigame sweep (overlay N)
  at frame F`). Pad inputs go through lockstep like any player's, so the sweep can't make the peers diverge.
  `sweep_sim.mjs` runs the page's pad code in node and prints the pattern frame by frame (checked: phases, button
  turns, stick, trigger turns).
- **Summary**: `minigames over 4 minutes: a: o40 313 s, ...`. A minigame that ran to its cap (or is still running at
  the end) shows up in the log without anyone having to time screenshots.
- **Port in use**: when `WEB_PORT` was taken (my second run on the same port), the script did `process.exit(1)`
  after Chromium had been spawned, so the browser was left running (I killed it by its own profile path, as its
  process group). It now goes through `finish()`, which kills the browser's process group and removes the profile.
  Seen working: the next collision left no profile and no browser behind.

## Runs

Test build only (`force.diff`, reverted before commit): `src/game/board/mg_setup.c` makes every round 2v2 and forces
the roulette's minigame (`N126_MG_DEFAULT`, 31 = m432, 26 = m427). The m427 "idle" build also zeroes the CPU
players' pad words in `src/REL/m427Dll/player.c`, so each boat moves only if its humans row. Builds:
`WEB_FAST=1 tools/web.sh`, copied to `build/n126-web-*`. Pairs: `node tools/web_pair.mjs $PB_DISC OUT SECS`, two tabs,
default CPUs (seats 2, 3), ~60 fps, one pair at a time.

| run | build | pad | result |
|---|---|---|---|
| m432-1 (900 s) | m432 | first sweep (all buttons every 16 f, stick round) | both m432 rounds 313 s = 5-minute cap; climbers moved a little, then stood at the crank/door stations. PASS, turn 3, no desync |
| m427-1 (900 s) | m427 | same | 4 m427 rounds, 72-81 s each: the CPU-partnered boat finishes. No evidence for or against the sweep. PASS, turn 5 |
| m427-idle-sweep (700 s) | m427 idle CPUs | phased sweep | both humans (Mario, Luigi) in the pink boat: it pulled ahead of the idle blue boat a little, then stuck against the left cave wall from 1'52" to 4'24" (`a-match-240/390.png`); race ended at its cap, 316 s |
| m432-2 (560 s) | m432 | phased sweep | the humans (Mario, Luigi) climbed past the first door blocks to the top section (`a-match-420.png`: 4'46", countdown 14), where m432-1's stood at the bottom; their CPU partners stayed at the doors, and the round still ran to the cap: 312 s. PAIR_SWEEP default, PASS, no desync |
| m432-3 (500 s) | m432 | final script | humans together on the red team this time: the all-CPU blue team reached its balloon at 1'56" (`a-match-240.png`) and won; both rounds 137 s and 129 s. So no line `minigames over 4 minutes` (none was), PASS, no desync |

Which seats share a team decides it (as the brief guessed). A team with no human finishes by itself and ends the
minigame. A team whose CPU partner waits on a human (m432's co-op stations), or a boat of two humans (m427), doesn't.

So the sweep finished neither of the two known minigames. It helps in m432 (the phased version gets the human
climbers much further than A taps or the first sweep did) but doesn't finish it. Balanced A/B rows a boat straight
into a wall. Steering
the river, or playing m432's co-op stations, needs the game state (the boat's heading against the next waypoint,
the station in front of the player). That means a read-only probe per minigame, like N104's
`pb_web_plunger_pad`. I didn't build that: both minigames end at their cap anyway, and a per-minigame bot for each
is a lot of code for test speed only.

## Checks

- `node --check tools/web_pair.mjs`; `sweep_sim.mjs` (the pattern).
- `tools/check.sh` passes.
- All runs: no desync, every run PASS.

## Not verified

- **A minigame with no time cap that A taps can't finish**: none found, and none tested. The sweep is meant for
  that case, but whether it would finish one is unknown. The grep survey can't rule out uncapped ones (see above).
- **The sweep finishing m427 or m432**: it didn't (see Runs).
- **A whole party (results) in the browser pair**: not run. The forced-minigame runs (10 to 15 minutes each) got to
  turns 2 to 5.
- **The `minigames over 4 minutes` line in a real run**: only m432-3 ran the final script, and none of its minigames
  went over 4 minutes, so the line (rightly) wasn't printed. m432-1/2 and m427-idle-sweep, which had such minigames,
  ran before it was added. Checked in node only: the summary expression on made-up overlay times gives
  `a: o40 313 s, o35 300 s (still running)`.
- **PAIR_TABS 3-4 and PAIR_MENU runs** with the sweep: not run. The pad code is the same in every mode.
- **PAIR_SWEEP=0** (no sweep): not run. It only skips the sweep (`!after`).
