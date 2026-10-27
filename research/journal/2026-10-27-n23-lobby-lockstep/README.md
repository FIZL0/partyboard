# N23: lockstep over the started lobby

## What

`net/src/lobby_c.rs` now starts the lockstep when a lobby reaches Started, on the lobby's own connections:

- **Line-up**: `Room::peers()` of the started room (host 0, then guests by first seat). `Config.seats[i]` is that
  index for a human seat, `NO_PEER` for a CPU seat (`peer == CPU`, the same test `Room::peers` uses).
- **Wire** (`lobby_c::Wire`, a `lockstep::Transport`): each connection task (host: `guest_conn`, guest:
  `run_guest`) now also carries datagrams: an outbox channel per connection in `Shared::dgrams`, and incoming ones
  go to `Shared::datagram_in`. The guest sends everything to the host, named by the receiver; the host delivers what
  is named `0` to its own session and relays the rest to that guest, renamed to the sender. Session inbox is a tokio
  unbounded channel read with `try_recv` from the game thread.
- **Delay**: the host reads `Peer::rtt` of each guest connection every 250 ms into `Shared::rtts`; at
  `Lobby::start` the delay is `delay_for(the two slowest guests' rtts summed)` (guest to guest goes through the
  host, so that is the slowest path; with one guest just its rtt). It travels in the final room: new
  `Room::delay` (`#[serde(default)]`), set by new `Host::start_with(seed, delay)` (`start` = `start_with(seed, 0)`).
  Every peer uses `room.delay.clamp(2, 15)`. **All peers must use the same delay**: `Session::new` treats frames
  below `delay` as idle for every remote, so a peer with a smaller delay would send real pads for frames the others
  run idle; that's why the host picks it and does not let each side measure its own.
- **Starter**: `Lobby::host/join` take a `Starter` (`Arc<dyn Fn(Config, Wire)>`). The C ABI passes
  `global_starter()` = `lockstep_c::start`; tests catch the pair instead (so they don't fight over the
  process-global session with `lockstep_c`'s test). `pbnet_lobby_leave` stops the lockstep if that lobby started it.
- `transport::Peer::recv_any` (new): message or datagram in one cancel-safe future, since `recv()` and
  `recv_datagram()` both take `&mut Peer` and can't share a `select!`.

## Things found

- **Peer names on lockstep datagrams are line-up indices in decimal, not endpoint IDs.** A lockstep datagram with a
  full backlog (pads up to `PAD_BUDGET` 1024 + header + 2 hashes + stamps) is ~1073 bytes; `datagram()` with a
  64-char endpoint ID adds 65, past `DATAGRAM_MAX` (1100), and `Peer::send_datagram` drops it silently. Only the
  big ones are dropped, and they're big exactly when a stall built a backlog, so the stall would never clear.
  With a 1–2 character index it is ≤ 1075.
- Datagrams that arrive before this side started its session are dropped (no inbox yet); the lockstep resends
  everything unacked, so a guest that hears Start later than another just catches up.

## Tested

`cargo test` (net): new `started_lobby_runs_the_lockstep`: three real endpoints in-process (relays off, DHT
testnet), host + 2 guests, start; checks each `Config` (me, peers 3, seats `[0,1,2,NO_PEER]`, delay = room's),
then steps the three `Session<Wire>`s for 90 frames: all ran identical pads, frames ≥ delay hold each seat's pads
(so guest↔guest relay works), no desync with equal hashes. `delay_from_the_slowest_two_guests` covers the delay
pick. Existing lobby tests updated for the starter argument. `tools/check.sh` passes.

## Not verified

- **Not run in the game**: no GMPE01 disc in `orig/GMPE01_00/`, and nothing on the C side starts a match from the
  lobby yet (filed as N27: build `NetplayMatch` from the started room and call `netplay_match_start`).
- **Over the internet / relays**: the test is loopback with relays off, so rtts are ~0 and delay is 2. A real
  delay > 2, relay paths and loss are not exercised over the network here (the lockstep's lag/loss behaviour is
  covered by `lockstep.rs`'s fake-wire tests).
- **rtt freshness**: the delay uses the last 250 ms reading of QUIC's smoothed rtt; nothing checks how good that
  estimate is right after joining.
- **A guest reconnecting after the start** keeps no wire (outboxes are snapshotted at the start): N10's.
