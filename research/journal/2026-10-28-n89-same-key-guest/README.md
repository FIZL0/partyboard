# N89: a guest with the host's player key takes a seat of its own

User report (2026-10-28): "when i joined it replaced player 1, there's a host and a player but only 1 player".
Two copies of Party Board on one PC share `<user dir>/online.json`. The guest's Hello carried the host's own
"Player 1" key. `Host::seat` gave back any seat whose key matched a Hello player's, as long as that seat was not the
joiner's own. So the guest took over seat 0 from the host: `peer` was rewritten to the guest and the host was left
with no seat.

## What changed

- `net/src/lobby.rs`
  - `Host::reclaimable(key)`: a Hello gets a seat back by key only when the computer holds that seat (`peer == CPU`,
    meaning its player is away). That only happens when continuing a save, or for a seat a dropped player left
    behind, which `gone` gives back to the save. A seat held by anyone who is here (the host's own local players,
    a connected guest) is never taken. A Hello with that key counts as a different player.
  - `Host::unique_key(key)`: a new seat whose key is already in the room gets `key-2`, `key-3`, ... instead. Keys
    must be unique in the room: `save.rs` writes them into online saves, Continue gives seats back by them, and
    `recover.rs` `Book::issue/rejoin` finds a token's seats by key. A shared key would hand the guest's mid-match
    rejoin the host's seat too.
  - The "lobby is full" count now uses `reclaimable` as well.
- `src/port/ui/online_model.{hpp,cpp}`
  - `claim_profile(dir)`: each running copy locks `online.lock` (flock / an unshared CreateFileW handle on Windows),
    held until the process exits. If another copy holds it, it tries `online-2.lock` → `online-2.json`, and so on
    up to 8, then falls back to `online.json`. A second copy gets a fresh default profile with random keys, so
    same-PC testing just works.
  - `adopt_keys(profile, view)`: a guest copies its seats' keys from the host's room into its profile.
- `src/port/ui/online.cpp`: `profile_path()` claims once per process (static). `LobbyWindow::refresh` stores keys
  the host changed in the profile file, so a later Continue gives the seat back. `OnlineWindow::save` keeps the
  keys that are on disk, so the window's older in-memory copy can't write the fresh key back over.

Decision: the fresh key is derived from the old one (`key-2`), not random. It is deterministic, easy to test,
and unique within the room. It only needs to be unique among the players of one match.

Behaviour change: before a match starts, a guest that reconnects on a new endpoint while its old connection is still
seated now gets a second seat. Before, it moved into its old seat. The old connection's seat opens when the
transport sees it drop (`gone`). The old test `leave_frees_seats_and_key_takes_seat_back` covered that move. It is
now `leave_frees_seats`, as the task says to reclaim only seats whose player is away. Mid-match rejoin goes by token
(`recover.rs`) and is unchanged.

## Tests

- `lobby::tests::a_key_whose_player_is_here_takes_a_new_seat`: on the in-memory JSON wire, a guest sends the host's
  key and gets seat 1 with key `key-h-2`, which it sees in its room. A second guest sends `key-h` plus its own key
  and gets `key-h-3`. All keys are unique, and all 4 seats are human at start.
- `lobby::tests::continue_reclaims_only_seats_of_absent_players`: resuming, a twin of the host's key gets the seat
  the computer played (3), not seat 0. g1 comes back by key. A twin of g1 gets an absent player's seat. Once g1 is
  gone, seat 1 goes back to the computer with g1's key, and g1 rejoins and reclaims it with the save's character.
- `lobby_c::tests::a_guest_with_the_hosts_key_gets_its_own_seat`: two real endpoints (local DHT testnet). The host
  keeps seat 0, the guest gets seat 1 with `key-h-2`, and the started room has 2 humans.
- The existing continue tests (`continue_gives_seats_back_by_key`, `continue_unclaimed_seats_...`,
  `continue_a_save_on_two_endpoints`) still pass. `tools/check.sh` passes.

## In-game check with the disc (PB_DISC, GMPE01-01)

Harness: `harness/n89_test.inc` is N88's SDL event and DOM-dump harness plus `lobby`, `writecode`, `prepjoin`,
`jointab`, `ready`, `players` (GWPlayerCfg) and `cat`. It was included temporarily from `src/port/ui/ui.cpp`, as in
N88's `test-hooks.diff`, but with system includes and `online.hpp` and `game/gamework_data.h` (inside
`extern "C"`) at the top of ui.cpp. `harness/run.sh <name> [samekey]` starts two copies with **one** user dir
(`build/n89-work/<name>`), and `online.json` has key `SHAREDKEY`. Copy 1 taps the launcher's Online, then Host
Game, and writes its code. Copy 2 writes that code into `online-2.json`'s `last_code`, then taps Online, the Join
tab, Join Game, and goes ready. The host taps Start Match. Logs are in `logs/`.

| run | copy 2's profile | lobby / match |
| --- | --- | --- |
| a | none: it claimed `online-2.json` (the code put there was used, so the claim worked), random keys | seats 0 host `SHAREDKEY`, 1 guest with its own key. Both peers: GWPlayerCfg 0 and 1 `iscom 0`, 2 and 3 CPU |
| b (samekey) | `online-2.json` pre-written with `SHAREDKEY` (as if the profile were shared) | seats 0 host `SHAREDKEY`, 1 guest `SHAREDKEY-2`. Both peers: 2 humans, 2 CPUs. `online-2.json` now holds `SHAREDKEY-2`, `online.json` still `SHAREDKEY` |

Harness pitfall: the first run's `prepjoin` used `sscanf("%199s")`. The profile path is longer than 199
characters, so it wrote JSON to a truncated path and the guest had no code. It now splits the line as a string.

## Not verified

- Windows lock (`CreateFileW` with share mode 0): no MSVC/Windows toolchain here. Only the Linux flock path ran.
- Android/iOS: only one copy runs there, so `online.json` is always claimed. Not run on a device.
- The behaviour before the fix was not reproduced in-game. The user's report and the code (`Host::seat` matched
  any key not held by the joiner itself) explain it, and the new tests fail against the old `seat`.
- The match was checked only up to its start (GWPlayerCfg after the board loaded), not played through.
- A Continue of a save written with a `-2` key by the second copy was not run in-game. It is covered by the unit
  logic: the key is in the save's `keys` and in `online-2.json`.
- Not looked at: the UI pixels, and what the lobby list shows when both copies use the same name ("Hosty" twice in
  run b).
