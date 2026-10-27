# N20: lobby glue (`net/src/lobby_c.rs`)

## What
The lobby on the network and its C ABI for N7's menu.

- `Lobby::host` / `Lobby::join` (Rust, the runtime `Handle` and the endpoint's bind future passed in so the tests use
  a local DHT testnet). Host: binds, `ready()`, makes the `lobby::Host`, shows the ticket, rolls a short code in the
  background (republished every `codes::REPUBLISH`, retried every 10 s on failure), accepts. Each accepted connection
  has a task: its messages go into `Host::on`, the host's answers are routed by peer name to each connection's outbox;
  when the stream ends, `Host::gone`. Guest: binds, dials (`codes::code_in` first, so links work), says Hello,
  feeds `Guest::on`; Refused / Leave / lost connection before the start close the lobby with words.
- One `Shared` (phase, role, me, ticket, short, error, outboxes, gen) behind a mutex. Menu commands (picks, ready,
  settings, cpu, start) run on the caller's thread against it, so `start` answers at once with the reason it can't.
- C ABI (`include/port/pbnet.h`): `pbnet_lobby_host(setup_json)`, `_join(setup_json, code)`, `_leave`, `_state`,
  `_gen`, `_json(buf, cap)` (snprintf-style snapshot incl. the whole `Room`), `_picks(chars, teams, n)`, `_ready`,
  `_settings`, `_cpu`, `_start(seed, why, cap)`. One lobby per process, on a 2-thread tokio runtime made on first use.

## Decisions (and why)
- **Setup as JSON, commands as plain args.** Setup has optional fields (relay, id, short) that will grow; the
  commands are fixed and simpler for C++ as ints/arrays. Room out as JSON, as the brief says.
- **Polling + `gen`**, not callbacks: the menu runs on the game thread; no C function pointers called from tokio
  threads.
- **Host is ready implicitly**: `pbnet_lobby_ready` returns 0 for the host (as `Room::all_ready` skips it).
- **Connections stay open after Start** (no Leave, no close): N8 runs the lockstep over them. A drop after the start
  only sets `error`, the phase stays `started` (N10's recovery). N8 still needs a way to reach the `Peer`s'
  datagrams from the started lobby; that hand-over is not written here (the per-connection task owns the `Peer`): filed as N23.
- **Refused joiners** are closed by the host 2 s after the refusal (opentennis does the same) so the message arrives.
- **Leaving** sends `Leave` (host: to every guest; guest: to the host, only before the start), waits 300 ms for it to
  go out, then closes the endpoint. A QUIC close drops unsent stream data, hence the linger.
- `start(0)` takes a seed from the clock (no RNG dependency).
- Default match id: first 8 chars of the host's endpoint ID + unix seconds.

## Tests (`cargo test lobby_c`, ~0.2 s)
- `two_endpoints_reach_start`: two real iroh endpoints in-process (relays off, local DHT testnet), guest joins by
  ticket, settings + pick seen by the guest, start refused until ready, a wrong-disc joiner refused with words, a
  third joiner leaves and its seat opens (and the guest's stale ready is dropped), then Start: both hold the same
  started room (seed, picks, CPU seats), host commands no-op after.
- `host_leaving_closes_guest`, bad code closes the joiner.
- `c_abi_without_lobby`: every C call with no lobby, snprintf truncation, bad setups refused.
Ran 5 times in a row, no flake.

## Tried and failed
- First build: `keep_code` held the `MutexGuard` across the sleep's `.await` (future not `Send`); scoped the lock.

## Not verified
- **Short codes through the lobby**: the tests run with `short: false` (no internet in the gate). The roll/publish
  itself is N2's and tested there on the testnet; the republish loop here is not exercised.
- **The C ABI with a real network**: `pbnet_lobby_host/join` bind with `Transport::bind` (public DHT, n0 relays),
  which the gate can't reach; only the Rust `Lobby` under it is tested over real endpoints. No C caller exists yet
  (N7).
- **LAN listing / browse** is not in this ABI: filed as N22.
- Nothing ran the game (no disc image needed for this task).
