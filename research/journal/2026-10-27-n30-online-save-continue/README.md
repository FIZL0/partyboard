# N30: N9's online save and continue, run with a GMPE01 disc

## Outcome

Everything N9 left unverified in the game works on the first try, without changes to the save code:

- **Save**: a `--script` run with `--match 0@900 --hash-log`, played through the first minigame (overlay 14),
  reaches resultDll (84) and logs
  `netplay: checkpoint at frame 9161, turn 1/10: <user dir>/online/local-0000000050424E54.json`.
  The file has `format` 1, id `local-0000000050424E54`, board 0, turn 1/10, keys `["local-1","","",""]`,
  the seeds, the started `--match` room and a 412-byte blob. Two save runs wrote the same blob.
- **Continue**: two `--script` runs with `--continue <that file> --match 0@900 --hash-log`. Run a is at the title screen
  at frame 900 (bootDll). Run b wanders first: skipBootSequence, modesel (74) at 4, Mini-Game mode (72) at 671.
  `hashdiff.py --from 900` gives **identical: 19100 frames**, overlays
  `-1@900, 89@902, -1@5268, 3@5269, -1@5546, 24@5547, -1@8620, 84@8621, -1@8862, 89@8863, -1@13942, 3@13943`.
  Both enter w01 (89) and log `board overlay 89 starts at frame 901, turn 1/10`, which is the saved turn. Both then
  play turn 1's remainder, the second minigame (overlay 24) and its result, and log
  `checkpoint at frame 8801, turn 2/10`. Their two new save files have the same blob (md5 389f29c9…), and the
  continue overwrote the same file name (the room id is kept). The heap layouts differ from 900 on (as in N26), but the
  state does not.
- No crash up to frame 20000 in the continue runs (turn 2, next instDll). N40's stack smashing at ~16077 (turn 2)
  happened with GWMGExplainSet(0) and other pads, and these runs did not reach that point in the same way.

## What changed

- `src/port/netplay_match.c`: the `board overlay N starts` log line also prints `turn T/M` (GWSystem). That is how
  the runs show that the continued board starts at the saved turn. Nothing else in the game changed.
- `tools/netplay/save-continue.py` (new) writes the three scripts used (`save`, `title`, `mgmode`). Each keeps its head
  script's lines before 900 (`mash-a.script` or `wander-mgmode.script`, so the menu paths are N26's). From 900 on,
  port 0 taps A every 20 frames. Inside the instDll window it taps START alone instead (save 6300-6510,
  continue 5340-5550). The generated scripts were checked to be identical to the ones the runs used.

## How the runs were made

- Build `linux-default-relwithdebinfo`, plus a **temporary, uncommitted** turbo:
  `if (getenv("PB_TURBO")) disableFrameLimiter = TRUE;` before the main loop in `src/game/main.c`. There is no flag
  for it. I filed **N46** (`--turbo`). With it a 20000-frame run takes under a minute. Turbo only skips the frame
  limiter's sleep, so the game's state per frame is the same.
- Each run had a fresh user dir under the worktree (`XDG_DATA_HOME`=`HOME`=`.n30-runs/<name>`, deleted afterwards)
  with `"video.enableVsync": false` in `MarioPartyRD/Party Board/config.json`. Run b also had
  `"backend.skipBootSequence": true`. The game ran in a private `Xwayland :83` (`SDL_VIDEODRIVER=x11`), with
  `timeout -s KILL`.
- Commands:
  ```
  partyboard --script save.script --match 0@900 --hash-log save.log
  partyboard --script cont-title.script  --continue save.json --match 0@900 --hash-log a.log
  partyboard --script cont-mgmode.script --continue save.json --match 0@900 --hash-log b.log   # skipBootSequence
  tools/netplay/hashdiff.py --from 900 a.log b.log
  ```

## Finding the frames (tried / failed)

- A-only pads from 900: board 902, instDll (3) at 6230, and it sat there to 20000 (it waits for START alone, as known).
- START taps 6300-8000: instDll left at 6506 and minigame 14 started at 6507. The START taps that went on into the
  minigame kept it paused: it was still in overlay 14 at frame 30000. So the window must end at the minigame's start.
- START 6300-6510: the minigame ran, then resultDll at 8954, the checkpoint at 9161, the board again at 9223 and the
  next instDll at 14562.
- First continue pair (A-only tail, no START window): already identical for 19100 frames, but it stopped in instDll at
  5269. The final pair adds the START window to play the second minigame and checkpoint.
- A continue probe with START taps 5340-7000 also got through minigame 24. It needed longer (resultDll at 9322 against
  8621), because the minigame was paused for a while.

## Not verified

- **Two real peers continuing over a lobby** (Host::resume, a guest rejoining by key): the menu (N7) does not offer
  Continue yet, so only the `--continue` debug path ran. Both runs were one process each on the same machine and
  binary.
- **A continue where seats change hands** (a CPU seat taken by a newcomer, or a human seat now played by the CPU):
  `--match` always has seat 0 human and 1-3 CPU, as the save does.
- **The match end and N31's save deletion**: the runs stop in turn 2, not at the end of the party (N40 still blocks
  that).
- **The second checkpoint's turn compared with the board's own counting**: the save is taken in resultDll with
  turn 1 and the board resumes at turn 1. That matches the offline continue (mentDll's SLLoadBoard path), but I did
  not compare it with an offline card continue on a real console.
- Comparing a continue with an **uninterrupted** run: after the first minigame, the original run took 5339 frames from
  the board to the next instDll and the continue took 4367. A continue resumes the board as the card continue does,
  not as the return from a minigame does, so the hash logs are not expected to match. Not investigated further.
- Windows/Apple/Android: not built (no toolchains), and `--script` pads only work where the PADRead wrap does.
