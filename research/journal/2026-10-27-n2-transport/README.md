# N2: transport (iroh QUIC, codes, LAN)

## What was done

Ported the transport layer of opentennis `crates/hst-base/src/net.rs` into `net/` without Bevy:

- `net/src/transport.rs`
  - `Relay` (n0 / own URL / off) with `parse` (the `relay = n0|own|off` + `relay_url` settings form) and
    `relay_url` validation, as opentennis.
  - `Transport::bind` / `bind_with(relay, dht)`: iroh endpoint (`presets::Minimal`, ALPN `partyboard/lobby/1`),
    addresses published on the mainline DHT (`DhtAddressLookup`, unfiltered so relay-off hosts can be dialled by ID),
    mDNS on service `pbn` added if it binds (opentennis B536: iOS without the multicast entitlement goes on without LAN).
  - `ready()` (wait for a home relay / direct address before making a code), `ticket()`, `publish_code()`,
    `accept()` (host), `dial(code)` (short code, ticket or bare ID), `set_lan_listing()` / `lan()` (a host's text
    listing in its mDNS user data, prefix `pb1`), `close()`.
  - `Peer<T>`: one bidirectional stream of messages (`send` queues, `recv` awaits; the reads run on their own task
    because a stream read is not cancel-safe, as in opentennis), datagrams beside it, `rtt()`, `close()`.
  - Wire framing kept from opentennis: stream messages are JSON with a little-endian u32 length first, max 1 MiB
    (`write_msg` / `read_msg`); datagrams carry `[len u8][peer name][data]` (`datagram` / `undatagram`), max 1100
    bytes (`DATAGRAM_MAX`, `fits`). The joiner opens the stream and speaks first (QUIC shows the host the stream only
    with its first bytes).
  - Message type is generic (`T: Serialize + DeserializeOwned`); the lobby's `Msg` enum belongs to a later task.
- `net/src/codes.rs`: short codes as opentennis (32 symbols, 4 long, key seeded from the code, BEP 44 mutable item
  holding a one-TXT DNS packet so it is also a pkarr record), `roll` (re-roll a code written less than 150 s ago),
  `publish` (DHT, plus the pkarr relay in the background), `resolve` (40 s of retries; DHT, then pkarr relay when the
  relays are on), `relay_get` / `relay_put` against `https://relay.pkarr.org` with an optional own `code_server`
  asked first (`set_code_server`), `code_ticket`, `is_code` / `code_in`.
- Differences from opentennis, on purpose: no `bevy::log` (`eprintln!("pbnet: …")`); no default own code server
  (opentennis's is theirs); DNS TXT name `_pbn` and key seed `pb-lobby-code/1/…` so our codes never collide with
  opentennis's; no WebRTC / wasm paths; no lobby list / DHT board / slots (not in this task).
- `cmake/pbnet.cmake`: Linux link adds `util rt` (from `--print native-static-libs` after the new deps); Apple adds
  Security + SystemConfiguration frameworks, Windows adds iphlpapi crypt32 secur32 ncrypt (what iroh's netwatch and
  rustls-platform-verifier use).

## Tests (`cargo test` in `net/`)

- `two_endpoints_connect_by_code_and_swap_messages`: host publishes a short code on a local n0-mainline `Testnet`,
  guest dials it, stream messages and datagrams go both ways, close ends the guest's stream, then a second join by the
  whole ticket. Relays off, loopback; ~50 ms.
- `stream_framing_is_u32_le_json`: raw bytes on the host side are `u32 LE len` + `{"Hello":{"name":"p"}}`.
- framing, relay settings, short code / DNS TXT round trips, freshness, unknown code error.
- `lan_listing_heard_by_mdns` (`#[ignore]`): passed here (`cargo test -- --ignored`), ignored by default since CI
  sandboxes often have no multicast.

Ran the suite 8 times in a row: no flakes. `tools/check.sh` passes (cargo test + CMake build, `libdol.so` relinked).

## What failed along the way

- `Peer::send` first moved `T` into the writer task: not `Send`-safe across the await for non-`Sync` `T`. Now `send`
  serializes up front and the task writes bytes.
- The worktree had no submodules (`extern/musyx`, aurora): `git submodule update --init --recursive` before the gate.

## Not verified

- Anything over the real internet: n0 relays, own relay, the public DHT, `relay.pkarr.org` GET/PUT. Tests use a
  local DHT testnet and relays off; `publish`/`resolve` skip the pkarr relay under `cfg(test)` so test codes never
  reach the real network. Needs two machines (or one with internet) and a small CLI or the C ABI (later task).
- Android and iOS builds: `cargo check --target aarch64-linux-android` / `aarch64-apple-ios` stop at blake3's and
  ring's C build scripts (no NDK / Xcode toolchain on this machine). The added Apple frameworks and Windows libs are
  not link-tested (no macOS/Windows link here); `x86_64-pc-windows-gnu` `cargo check` passes.
- Android TLS: reqwest uses rustls-platform-verifier, which on Android needs JNI init (opentennis does it in
  `android.rs`). Short codes through the pkarr relay will fail on Android until that is wired; DHT codes don't need it.
- The C side does not call the transport yet, so `--gc-sections` drops most of it from `libdol.so`; a link that keeps
  iroh live comes with the C ABI task.
- The game itself: not run (nothing here touches it; no disc image needed for this task).
