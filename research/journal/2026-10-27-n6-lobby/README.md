# N6: lobby protocol (`net/src/lobby.rs`)

## What
Host-authoritative lobby, ported from opentennis `hst-base/src/net.rs` (`Msg`, `Room`, `Hello`/`Refused`/
`Room`/`Start` flow, the build check's wording) without Bevy, mods, countdown, chat, devices or rejoin-mid-match.

- `Room`: match id, 4 seats (`peer`, name, stable player key, character, cpu, CPU difficulty, team), board,
  turns, bonus stars, teams, per-seat handicaps, seed, ready set, `ver`, `started`.
- `Msg`: `Hello { ident: {build, disc}, players: [{name, key}] }`, `Room`, `Refused`, `Picks`,
  `Ready { ver, ready }`, `Start(Room)`, `Leave`. JSON on the existing `transport::Peer<Msg>` stream.
- Sans-IO: `Host::on(peer, msg) -> Vec<(To, Msg)>` plus host commands (`settings`, `picks`, `cpu`, `start`,
  `gone`); `Guest::on(msg) -> Event`. The transport task and the C ABI (N7) only move bytes.

## Decisions (and why)
- **`ver` bumps and ready clears on every seat/settings change**, readiness alone does not bump it. A `Ready`
  for an old `ver` is answered with the current room, not applied: a guest is only ever ready for what it saw
  (opentennis's `Ready(ver)` idea).
- **Characters are unique** (MP4 rule). A pick of a taken character keeps the seat's old one; the guest gets the
  room back so its menu snaps to the truth. Join and CPU seats take the lowest free character.
- **Peers with several local players** (Hello carries 1..4 players) take that many seats; refused "full" if they
  don't fit. Lockstep's `Config.seats` already supports this.
- **Player key in the Hello takes its seat back** before the start (new connection, same player: seat, pick and
  team kept, old peer's ready dropped). Small, and N9/N10 build on the same key.
- **Handicaps on the Room** (per seat index), as the brief lists them apart from seats.
- `teams: bool` added beside `Seat::team`, otherwise team is meaningless; default team = seat / 2.
- `Room::peers()` (host first, then guests by first seat) is the line-up order for `lockstep::Config`, so N8
  doesn't have to invent one.
- Start refuses unless every seated guest is ready; open seats become CPU (normal difficulty, free character).
- Disc revision is an opaque string (`GMPE01-00`); C supplies it (N7/N8).
- The host's lobby id (`Room::id`) is passed in by the caller (no RNG in the crate).

## Tests (cargo, `lobby::tests`, every message round-trips JSON)
4 peers join/pick/ready/start and all see the host's exact Room; empty seats → CPU with distinct characters;
build and disc mismatch refused with nobody seated; two local players take two seats and full refuses; taken
character kept, stale ready ignored, settings change clears ready; leave frees seats and the key takes a seat
back; settings clamp, CPU seats, human seats protected; nothing changes after start.

## Not verified
- No network run: the protocol is only exercised over an in-memory wire (JSON round-trip), not over
  `transport::Peer` between real iroh endpoints. The async task wiring it to the transport, and the C ABI, are
  not written here (N7 needs them; N2's transport test covers the stream framing itself).
- Board/turn/handicap limits (`BOARDS = 6`, 10..50 by 5, handicap ≤ 9, 4 difficulties) are from knowledge of
  MP4's menus, not checked against `mentDll`; N7/N8 should confirm against `src/REL/mentDll/main.c`.
- Nothing ran the game (no GMPE01 disc needed or used for this task).
