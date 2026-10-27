# N27: from the started lobby to the match

## What was done

- `netplay_match_from_lobby(json, &m, &local_seats)` (src/port/netplay.c) reads `pbnet_lobby_json` into a
  `NetplayMatch`: board, turns, bonus, teams, handicaps[4], seed (full u64), every seat's character / cpu /
  difficulty / team, and this machine's seats (`"seats"`, as a bit mask). It needs `state == "started"`,
  `room.started == true`, four filled seats with every field, at least one local seat, and
  `netplay_match_invalid() == NULL`; otherwise it returns why and leaves `m` untouched.
- The reader is a small hand-written JSON reader in C, made for what serde_json writes there (objects, arrays,
  strings, unsigned integers, true/false/null). Why not reuse nlohmann (online_model.cpp): the seed is a u64
  (e.g. 18364758544493064720) and the brief puts the glue in netplay.c (C, and the C tests link netplay.c alone).
  nlohmann would read u64 fine, but it would mean a C++ unit in the C test harness.
- `netplay_lobby_poll()` runs every loop from `ui_update` (src/port/ui/compat.cpp), i.e. on the game thread
  between game ticks, also during lockstep WAITs. The first time it sees `PBNET_LOBBY_STARTED` it reads the room
  and calls `netplay_match_start`; once per started lobby (re-armed when the state leaves STARTED). If the room
  cannot be played it logs why and calls `pbnet_lobby_leave`, so the other peers see the lobby close instead of
  waiting forever on this peer's pads. Not hooked into src/game: `ui_update` is port code, and `main.c` already
  calls it in both the tick and the WAIT path. The prelaunch loop calls `partyboard::ui::update()` directly, so no
  match is started before the game has booted.
- "This machine's seats" do not go into `NetplayMatch`: pbnet already feeds local pad i into this machine's i-th
  seat and `out[i]` is seat i (netplay_tick_begin), and `party_config` sets `pad_idx = i`. They are logged and
  a started lobby with none is refused.
- Format pinned on both sides: `net/src/lobby_c.rs` `started_json_fixture` builds a started host room (teams,
  handicaps, a CPU at difficulty 3, open seats filled by start, a name with escaped quotes, a seed above 2^63) and
  asserts its `Snapshot` JSON equals `tools/netplay/lobby_started.json`; `tools/netplay/tick_gate_test.c` reads
  that same file and checks every field, plus edits of it (white space, member order, other local seats, open
  seat, wrong state, not started, out-of-range values, seed overflow / fraction, 3-vs-1 teams, truncated or
  trailing input) and the poll's start-once / re-arm / leave-on-bad-room behaviour. Both run in tools/check.sh.

## Tried and dropped

- Starting the match from `LobbyWindow::update` (it already sees Started and hides itself): the window may be
  closed or not open at all when the host starts, so a global poll is used instead.

## Follow-ups

- Match end is not handled by anyone (nothing calls `netplay_end`; leaving a started lobby mid-match only stops
  the lockstep). Filed as N30.
- In-game run with a disc noted on N26.

## Not verified

- An actual online match start in the game (two processes, lobby, board entry, hash logs agreeing): no GMPE01
  disc image in `orig/GMPE01_00/`. Code and tests only; noted on N26.
- The poll against the real pbnet lobby across processes: the C test uses a fake pbnet with the JSON pinned to
  the real serializer, not a live lobby.
- Windows/macOS/Android/iOS builds of the compat.cpp include: no toolchains here; only the Linux build ran.
