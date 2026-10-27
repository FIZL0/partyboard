# N47: autosave every turn (online and offline)

User (2026-10-27): "if it crashes or something happens we can continue".

## What landed

**Online: the save follows every player's turn.** N46 already took the board state (the `SLSaveBoard` blob) at the
start of every player's turn (`netplay_turn_checkpoint`, hooked in `MainFunc` right after
`GWSystem.player_curr = i`), but only as an in-memory resync point. Now that hook first calls N9's
`netplay_checkpoint()`, during any match (lockstep or a `--match` run). So every peer writes
`<user dir>/online/<match id>.json` at every turn start, as well as after every minigame (resultDll, unchanged). Then,
with lockstep, the same blob (`netplay_checkpoint_last`) becomes the resync point. Continue (N9's `Host::resume` +
`netplay_match_continue`) needs no change: the file is the newest checkpoint, and N46's `netplay_turn_resume_arm` /
`BoardStartCheck` makes a turn blob resume the saved player's turn. Seating by player key is N9's.

- Atomic write: `Save::write` (`net/src/save.rs`) already wrote a temp file and renamed it. It now also `sync_all`s
  the temp file before the rename, so a power loss right after the rename can't leave a short file.
- The checkpoint log line now also prints `player N`.
- `net/src/lobby.rs` needed no change. The lobby carries the host's save (`Room::checkpoint`), and every peer writes
  the same frames in lockstep.

**Offline: a local party autosave, offered by the game's own party Continue** (`src/port/netplay_autosave.c`, new):
- `netplay_party_autosave`: the same turn hook, outside a match, in a party (`GWSystem.party`), not the tutorial board
  (flag 1,11). It writes `<user dir>/local/party.json` with `format`, `time`, `board`, `turn`, `max_turn`, `player`,
  `blob` (hex, big-endian as on the card). The write goes through `netplay_file_replace` (`netplay_dir.cpp`): temp file,
  fsync/`_commit`, `std::filesystem::rename`, which replaces the file on Windows too.
- `netplay_party_offer` (hook in mentDll `fn_1_85C8`, just before `if (GWGameStat.party_continue == 1)`, which is the
  party menu's "continue?" question): if the autosave can be read and is a party-board blob, it goes into the card
  buffer's party board, `party_continue` is set to 1, and the turn resume is armed. The game's own question then comes
  up, and its "yes" (`SLLoadBoard`, `saveExecF = 1`) loads the autosave. The board resumes the saved player's turn
  through the same N46 path as online.
- `netplay_party_finish` (hook in mstory3Dll `fn_1_17570`, the party branch before the results' save process): the
  autosave is deleted. If the offer turned `party_continue` on (it was off), it is turned off again.
- Guards: the board's three turn hooks used `if (netplay_active())`. They now use `netplay_turn_hooks()`, which is
  `netplay_active() || GWSystem.party == 1`, because offline autosave has to run without netplay. Each hook does
  nothing offline unless it has work to do: `netplay_turn_resumed`/`netplay_turn_resuming` only act when armed, and the
  checkpoint only writes a file. The mentDll and mstory3Dll hooks are one line each, under `#ifdef TARGET_PC`. This is
  the one place where "offline play must be unchanged" gives way, because the task asks for it. The game state is
  unchanged. Only the file and the Continue offer are new.
- Online texts (`online.cpp`): Continue says "A match is saved at every player's turn", and Rejoin / Host Match say
  they go on from the last turn, not "from the last minigame".

Decisions made unattended:
- **The autosave wins over the card's own party save.** It is always at least as new within a party. It replaces the
  card buffer's party board in memory, so a later card `SLSave` stores the autosave's state as the card's party.
  If the card held an older, different party and the player declines, that party is lost. I accepted that because a
  party crash is the case being fixed.
- **One autosave file**, not one per party: the game has one party Continue slot.
- **A party quit mid-way** (no results) keeps its autosave, and it is offered next time, as a crash is.
- The resume arm stays set when the player says "no". The next board creation disarms it (`netplay_turn_resumed`
  after `CreateBoard`), and a new party has flag (1,1) clear, so it never resumes from it.

## Tests

- `tools/netplay/autosave_test.sh` (new, in `tools/check.sh`) builds the real `netplay_autosave.c`,
  `netplay_resync.c`, `netplay_save.c` and `netplay_dir.cpp`'s file helpers, with a fake SLSaveBoard and pbnet, in a
  temporary user dir. It checks:
  - every offline turn start replaces `local/party.json`, no `.tmp` is left, the card buffer is unchanged, and no
    online save is written
  - story mode and the tutorial board write nothing
  - offer + `SLLoadBoard` gives the saved turn/player/coins, and the resume is armed, then disarmed
  - finish deletes the file and turns off the question it turned on, but leaves a question the card had on
  - damaged, other-format and short files are not offered
  - during a match, every turn is an online save (plus a resync point with lockstep, the same blob) and never the
    autosave
  - finish during a match leaves the autosave alone
  - `netplay_turn_hooks` is on while netplay is active or in any party
- `cargo test` (save round trip with the synced write), clippy clean. `tools/check.sh` passes.

In-game, GMPE01 (`$PB_DISC`), relwithdebinfo, each run with its own `XDG_DATA_HOME`/`HOME` under `.n47-runs/`
(deleted afterwards), vsync off, plus a **temporary, uncommitted** turbo (`PB_TURBO` -> `disableFrameLimiter`, as N30).
Scripts: `harness/run.sh`, `harness/crash.sh`, and N30's `save-continue.py`.

1. **Per-turn online saves**: `--script save.script --match 0@900`. Checkpoints were written at frames 3551, 3931,
   4342 and 5248 (turn 1, players 0-3), 9161 (after the minigame), then 9233 / 10233 / 11520 / 12765 (turn 2, players
   0-3).
2. **Kill mid-board, continue, hashes equal**: the same run, `SIGKILL`ed as soon as the save's `frame` reached 4342
   (turn 1, player 2). The file was intact (format 1, 824 hex chars). Both continues used that file, one in the killed
   peer's user dir and one in the other peer's: `--continue <it> --match 0@900` with A taps. Both logged "the board
   resumes at player 2's turn" and wrote checkpoints for players 2 and 3 at the same frames (908, 1284). Both went into
   instDll at 2289 (no extra turns for players 0/1). `hashdiff.py --from 900`: **identical, 9100 frames**.
3. **Offline unchanged**: `--script tools/netplay/mash-a.script --hash-log` (13200 frames, an offline w01 party). It
   ends at hash **49E98D41456DA884**, the same final hash N46 recorded for the pre-N46 and N46 trees. It also logs
   a party autosave at turn 1 for players 0-3.
4. **Offline crash + Continue**: mash-a, `SIGKILL` once `local/party.json` said player 2. The relaunch with the same
   pads logged "party autosave offered (board 0)" and "the board resumes at player 2's turn". It played players 2 and
   3, the minigame (ovl 31), the results (84), and went back to the board at turn 2, player 0.
5. **Party end deletes it**: the autosave was edited to turn 20/20 and continued. A **temporary** pad hack pressed
   START in instDll/mstory3Dll, because fixed START windows miss in these runs (their timing varies, N53). The run
   played the last turn and the results, then logged "party over: its autosave is deleted". The A taps then started
   a new party from turn 1, and no Continue was offered.

## Tried / failed

- A **real two-peer kill/Continue** (N38's netns harness, `harness/n47_harness.inc` with roles host/guest/conthost;
  the guest obeys `cmd-leave`/`cmd-join`) never reached a turn. Every lobby match in this slot **desynced at frame
  1080 on w01**. The first differing section is `brand` (BoardRand) at frame ~1077, and it recurs every epoch, so N46
  resyncs from the match start again and again. The same happened with N38's `netplay_match.c` fix applied
  (temporarily: it is on main, not on this branch) and also **with N47's board/resync/menu hooks reverted**. So it
  predates N47. N38's own runs (slot s3) did not show it. Filed as **N55**. Both peers' desync input logs of
  one such run are kept in `desync-1080/` (gzip; the N55 entry says they were deleted, they were copied here).
- The first local runs gave relative `--hash-log` paths, and the run script `cd`s into the user dir. Use absolute
  paths.
- `pkill -n partyboard` would hit other slots' games. The crash script kills only the pid whose cwd is its own run
  dir.

## Not verified

- **Two real peers: kill one mid-board, relaunch, Continue, hashes match the other peer.** This is blocked by the
  frame-1080 desync above (N55). The single-process substitute (test 2) runs the same `netplay_match_continue` path
  from the killed run's file in two user dirs. It does not cover the lobby part: Host Match from the Continue tab, the
  guest leaving the dead match and joining by code, and seats by key. That part is N9's, tested with two real
  endpoints in `lobby_c` tests.
- **The two peers' saves being byte-equal at the same turn**: lockstep makes them so, but it was not run (see above).
- **The Continue question by eye**: the offline continue was driven by scripted A taps; no screenshot (windows are
  on a hidden workspace).
- **Two offline continues compared by hash**: offline `--script` runs vary in timing (N53), so it was not tried.
  Only one offline continue was run per case.
- **A crash during the write itself** (between the temp write and the rename) was not produced. The temp + sync +
  rename order is what guards it, and the test checks that no `.tmp` is left.
- **Story mode** (writes nothing) and the **tutorial** were only covered by the C test, not in the game.
- Windows (`_wfopen`/`_commit`), Apple and Android: not built (no toolchains). The file helpers are std C++/C.
