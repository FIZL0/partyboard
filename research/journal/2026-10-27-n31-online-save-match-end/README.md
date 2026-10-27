# N31: online save at match end

## What was done

An online party's save (`<user dir>/online/<match id>.json`, N9) was rewritten at every checkpoint and never removed,
so after the last minigame's checkpoint a finished match stayed in Continue. Now every peer deletes its own copy when
the match is over:

- **When**: `netplay_ovl_start` (`src/port/netplay_match.c`), at the moment N25 marks the match over (a menu overlay
  starts after the board), on every peer. It calls `netplay_checkpoint_finish()` (`src/port/netplay_save.c`) right
  after it logs `match over`. This is still inside the match: the lobby's room is the started one (N25's
  `pbnet_lobby_back` reopens it under a *new* id only later, in `match_finish` at the end of the frame), and a
  `--script` run (which keeps its session) goes through the same place. Offline play: the hook already sat behind the
  board/over checks of a running match; no new hook in `src/game`.
- **By the match id**: new `pbnet_save_finish(user_dir, room_json)` (`net/src/save.rs`, stub in
  `src/port/pbnet_stub.c`, declared in `include/port/pbnet.h`): the id is the lobby's started room's, else the
  `room_json`'s (the `--match` room, as `pbnet_save_write` uses), and it removes `online/<file_name(id)>`. By id and
  not by "the path I last wrote" because a continue keeps the room id (`Host::resume`) and a peer can reach the end
  of a continued match without writing a checkpoint of its own in this session (e.g. a continue of a save whose last
  checkpoint was after the last minigame: exactly the stale saves this task is about).
- **Fallback by path**: if `pbnet_save_finish` finds no file (or no started room: a lobby that closed during the
  match, where the `--match` room's `local-…` id is not the lobby's), the file this match's last successful
  checkpoint wrote is deleted with the existing `pbnet_save_delete`. That path is remembered in `netplay_save.c` and
  forgotten when a match starts/ends (`netplay_checkpoint_set(NULL, 0)`) and after a finish.

Deleting rather than marking finished: nothing reads a finished save (Continue is the only consumer, and the room/
results are not shown anywhere afterwards), and deleting needs no format change.

## Tests

- `cargo test` (net): `save::tests::finished_match_leaves_no_save`: two saves, finish by a room JSON deletes only that
  match's, a second finish returns 0, no room / no dir return 0. `clippy --all-targets` clean.
- `tools/netplay/match_test.c`: the N25 end calls `pbnet_save_finish` once with the user dir and the started room
  (no checkpoint, so no path fallback; a second menu start does not call it again); after a written checkpoint and
  finish returning 0, the checkpoint's path is deleted; a continued match ends by id without touching the previous
  match's path.
- `tools/check.sh` passes.

## Not verified

- **In the game**: no party can be played to its end yet (N40: instDll waits for START, stack smashing in turn 2),
  so neither the `netplay: match over: its online save is deleted` line nor the Continue list after a real match
  was seen. Once N40 lands: play a `--match` party (fresh user dir) to the end and check `online/` is empty.
- **Two peers**: each peer deletes its own file from the same hook; not run with two processes (no command line
  host/join; see N25's notes and N38).
- **A crash between the last checkpoint and the end** still leaves the save (nothing ran the end); a continue of it
  replays the end of the party and then deletes it. Not filtered out of the list on purpose (the offline card behaves
  the same).
- Windows/Apple/Android builds: not built (no toolchains); the stub got the new function.
