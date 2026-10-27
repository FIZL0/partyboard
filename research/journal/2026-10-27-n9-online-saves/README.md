# N9: online saves and continue

## What was done

**Save** (every peer, at every board checkpoint):

- Hook in `src/REL/resultDll/main.c` (`ResultMain`, just before the `GWSaveModeGet() != 1 && SLSaveFlagGet()`
  branch that runs `SaveExec`): `if (netplay_active()) netplay_checkpoint();`, under `TARGET_PC`. That is the
  point where the offline game calls `SLSaveBoard` after a minigame. Online the save mode is 1 (N8), so `SaveExec`
  never runs and the local card is never written.
- `netplay_checkpoint` (`src/port/netplay_save.c`, new): only during a match (`netplay_match_running`). Takes the
  SLSaveBoard blob (`netplay_board_blob_get`): **SLSaveBoard itself**, into `saveBuf`, whose party-board part
  (`offsetof(SaveBufData, system)` .. end of `player[4]`, 412 bytes = 0xDC + 4 x 0x30) is backed up before and put
  back after, so the card buffer of the player's own save is never changed by an online match (a later menu
  `SLSave` writes the player's offline party, not the online one). The blob is big-endian (SLSaveBoard's
  byteswap), so a save from one platform loads on another.
- `pbnet_save_write` (`net/src/save.rs`, new) writes `<user dir>/online/<match id>.json` (temp file + rename):
  `format`, `id`, `time` (Unix s), `frame`, `board`/`turn`/`max_turn`, `keys` (seat -> player key, "" for a CPU
  seat), `seeds` (match = Room.seed, session, frand, rand8, BoardRand), `room` (the started Room; its seats carry
  peer, name, key, character, cpu, difficulty, team), `blob` (hex). The Room is the process lobby's started room
  (`lobby_c::started_room`); with no lobby (a `--match` debug run) C passes `netplay_match_room_json` (id
  `local-<seed>`, seat keys `local-<n>`). A failed write is logged and the match goes on.
- The user dir: `netplay_user_dir()` (`src/port/netplay_dir.cpp`, new) = `PartyBoard_ConfigPath`, the dir the
  config is in.
- The last blob is also kept in memory (`netplay_checkpoint_last`) for N10/N11's reload; cleared at match start and
  end, set to the save's blob by a continue.

**Continue**:

- Lobby (`net/src/lobby.rs`): `Host::resume(saved_room, checkpoint, ident, players)`. The room is the save's (same
  id, so the continue's checkpoints overwrite the same file; same characters, teams, rules), with
  `Room::checkpoint` = blob + turn + time, so **guests need no save of their own** (any peer holding the save can
  host). Every seat starts as the CPU's, keeping its player's key. Seating (`Host::seat`): a key that holds a seat
  takes it back from the CPU; anyone else takes a free (CPU) seat, the ones the CPU played in the save first, then an
  absent player's (whose key is then replaced by the newcomer's, so the next save gives it to them). Picks and
  settings are refused while resuming; `cpu(i, Some(d))` only changes a free seat's CPU level; a guest leaving puts
  its seats back as the save had them. At `start` the seats nobody took stay CPU. `Room::checkpoint` is skipped in
  JSON when None, so a new match's Room is unchanged on the wire.
- C ABI (`include/port/pbnet.h`, stubs in `src/port/pbnet_stub.c`): setup `"resume": "<save path>"` for
  `pbnet_lobby_host` (a save that can't be read closes the lobby with "Couldn't continue the save: ..."),
  `pbnet_lobby_checkpoint` (the started room's blob), `pbnet_save_list` (JSON summaries, newest first, unreadable
  files skipped), `pbnet_save_blob`, `pbnet_save_delete` (only `*.json` in an `online/` dir), `pbnet_save_write`.
- Game (`src/port/netplay_match.c`): `netplay_match_continue(m, blob, size)`: checks the size and the blob's board
  (decoded with SLLoadBoard, state restored), then the same path as `netplay_match_start` with the board from the
  blob. In the tick, instead of the new party: GWSystem/GWPlayer/GWPlayerCfg zeroed, then **SLLoadBoard of the blob**
  (as mentDll's continue, `main.c:798-805`), save mode 1, seat i on pad i, CPU flag/level from `m` (who plays the
  seat now), `BoardMGSetupPlayClear()`, `saveExecF = 0`. Pads, GWGameStat, wipe, the start process and the heap
  scrub are N8's, unchanged.
- Debug: `--continue <save.json>` with `--match <b>@<frame>` (and `--script`) continues that save at that frame
  (board from the save).

What the menu (N7) has to do for a continue: list `pbnet_save_list`, host with `"resume": path`, and on
`PBNET_LOBBY_STARTED` with `pbnet_lobby_checkpoint(...) > 0` call `netplay_match_continue` with the Room's seats
(cpu, difficulty) and seed instead of `netplay_match_start`. Every peer, host or guest, does the same.

## Findings: what differs on a continue, and the earliest common state

- **The board's "recently played minigames" lists** (`mgPlay4P`, `mgPlay1Vs3`, `mgPlay2Vs2`, `mgPlayBattle`,
  dol statics in `board/mg_setup.c`) are cleared by `BoardObjectSetup` only when flag (1,0) is clear
  (`board/main.c:102`). A new party has it clear (N8 zeroes GWSystem); **a checkpoint has it set**, so on a continue
  the board keeps whatever lists that peer last had: the host who played the match up to here has the match's,
  a guest who restarted the app has none, a peer who played offline since has that party's. The minigame pick
  then differs. The continue clears them (`BoardMGSetupPlayClear`), which is also what a console continue after a
  power-off has.
- `saveExecF`: mentDll's continue sets it to 1, and mstory3dll's end of party then clears `party_continue` and
  `SLSave`s the card. Set to 0 on a continue so the end of an online match never writes the local card (it is
  `SLSaveFlagGet`-guarded too, but the flag is the player's card state, not the match's).
- The blob holds `GWPlayer.port/com/diff` of the saved match; who plays a seat can change on a continue, so those
  (and `GWPlayerCfg`) are overwritten from the new Room after SLLoadBoard. Characters and teams are not (the room
  locks them to the save's).
- Everything else the board reads at start is reset as by N8 (pads, GWGameStat canned, wipe, overlay history,
  free heap memory zeroed), so the continue's earliest common state is N8's with GWSystem/GWPlayer from the blob.

## Tests

- `cargo test` (net): `save.rs` round trip (write, read, list, overwrite at the next checkpoint, no temp left),
  damaged/foreign/future-format files skipped, unstarted room or empty blob refused, safe file names, C ABI
  write/list/blob/delete (delete refuses paths outside `online/`); `lobby.rs` continue: seats back by key from new
  endpoints with the host being a former guest, characters/teams/rules as saved, guests see the checkpoint;
  strangers take CPU-played seats first then an absent player's (re-keyed), picks/settings refused, CPU level only,
  a leaver's seat goes back to the save's, full lobby refused, untaken seats stay CPU at start; resume of an
  unstarted room refused; `lobby_c.rs` **two real endpoints continue a save** (the old guest hosts, the old host
  rejoins by key, both get the same started room with the checkpoint) and a missing save closes the lobby.
- `tools/netplay/match_test.sh` (in `tools/check.sh`) now also links `netplay_save.c` with a fake
  SLSaveBoard/SLLoadBoard (saveload.c's, minus the byteswap) and a fake `pbnet_save_write`: no write outside a match;
  the checkpoint sends the blob of GWSystem/GWPlayer, the seeds, the user dir and a room JSON; `saveBuf` unchanged;
  a failed write keeps the blob; continue refuses a wrong size and a board-6 blob; on a peer with stale state and a
  different card it restores turn/coins/characters/flags, sets pads, CPU seats and levels from `m`, clears the play
  lists, `saveExecF` 0, card buffer unchanged, calls `w01 + saved board`. The room JSON it writes is then parsed by
  pbnet's own `Room` (`save::tests::c_room_json_is_a_room`, run by match_test.sh with `MATCH_TEST_ROOM`).
- `tick_gate_test` got stubs for the new calls in `netplay.c`; passes. `tools/check.sh` passes; `libdol.so` exports
  the new functions, `resultDll.so` imports `netplay_checkpoint`, `check-hook-imports.sh` passes; `pbnet_stub.c`
  compiles with -Wall -Wextra clean.

## Tried / failed

- First test build of the lobby tests failed: settings/CPU changes made with `n.host.settings(...)` without
  delivering the new Room left the guests' Ready for an old `ver`. Delivered in the helper.
- `run_host` first held the `Shared` lock across `t.close().await` on a bad save (not `Send`); the save is now read
  before taking the lock.
- Considered keeping unclaimed seats as a third kind of seat ("saved", human but absent): every place that lists
  peers (`Room::peers`, `all_ready`, lockstep's line-up) would have had to skip it. Unclaimed seats are CPU seats
  that keep their key instead, which is also what "played by CPU" asks for.
- Considered writing the JSON from C: it needs the Room (Rust has it) and a reader for the continue anyway, so the
  file format lives in Rust only and C passes bytes.

## Not verified

- **No game run** (no GMPE01 image in `orig/GMPE01_00/`): nothing ran in the game. Not checked: that `resultDll`
  is reached after every minigame of an online party and the checkpoint is written there; that the board resumes
  correctly from a continued state started from a menu process (mentDll's path is mirrored, not run); that two peers
  continuing give identical hash logs; whether other board dol statics besides the play lists survive into a
  continue and differ. Filed as **N27** with the steps (`--script ... --match 0@900 --hash-log`, play past a minigame;
  then `--continue <save> --match 0@900` twice, `hashdiff.py --from 900`).
- **No two-peer game run**: the lobby continue is tested over two real endpoints in-process, but nothing calls
  `netplay_match_continue` from a real Room yet (N7 menu); the Room -> `NetplayMatch` and the
  `pbnet_lobby_checkpoint` call are the menu's.
- The checkpoint after the **last** minigame is saved too, and nothing removes the save when the match ends, so
  Continue would offer a finished match: filed as **N28** (after N25 match end).
- A host whose local players have no seat in the save and find none free gets no seat (spectates); not handled in
  the menu, not tested beyond the lobby refusing a full guest.
- Saves are not pruned; a desync (N11) reload from `netplay_checkpoint_last` is not wired (N11's).
- Windows/Apple/Android builds not built (no SDKs); the new code is plain C, a few lines of C++ (std::filesystem, as portmain.cpp) and Rust std::fs. The tvOS stub
  build (PBNET_STUB) was already broken before this task by N20's missing `pbnet_lobby_*` stubs: filed as **N29**;
  N9's own new functions are in the stub.
