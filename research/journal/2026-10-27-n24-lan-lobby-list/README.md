# N24: LAN lobby list in the C ABI

For N7's "LAN games" tab (the tab itself is N28). Code: `net/src/lobby_c.rs` (section `LAN`, C ABI
`pbnet_lan_browse` / `pbnet_lan_stop`), `include/port/pbnet.h`, `src/port/pbnet_stub.c`.

## What it does

- **Host listing.** An open host lobby puts `{"id","name","open"}` (room id, first local player's name, seats
  that are `None`; CPU seats count as taken) in its endpoint's mDNS record through `Transport::set_lan_listing`.
  `Shared::bump` (called on every change) recomputes it and sets it only when it changed; it is `None` once the
  phase leaves `Open` (start, close), so a started or closed lobby drops off the list.
  - `set_lan_listing` → iroh `publish_my_addr` runs from the menu's thread too (host commands bump under the
    caller's thread), so `Shared` keeps the runtime `Handle` and `enter()`s it around the call, in case the
    address lookups spawn.
  - mDNS user data is at most 245 bytes (`iroh_dns::UserData::MAX_LENGTH`), less the `pb1` tag: `Listing::fit`
    shortens the name by chars until the JSON fits; a match id too long to fit at all means no listing (logged).
- **Browse.** `Browse` binds an endpoint with no lobby on pbnet's runtime and lists `Transport::lan()` parsed back
  into listings (records that aren't listings are skipped), with `state` binding/open/closed and `lan` (mDNS on;
  false on e.g. an iPhone without the multicast entitlement). The bound endpoint sits in a tokio mutex that the bind
  task holds until it is done, so `list()` uses `try_lock` (busy = "binding") and never waits.
- **Join reuses it.** `pbnet_lobby_join` takes the global browse (`Browse::or_bind`): if its relay setting matches
  the join's, the lobby dials from that endpoint (waiting for the bind if it is still going), else it binds a new
  one as before. A taken `Browse` swaps an empty slot in so its `Drop` (which closes an untaken endpoint) closes
  nothing. `pbnet_lobby_host` ends the browse as well (the host's own endpoint is the one listed; an idle second
  endpoint was just waste). A `pbnet_lan_browse` after a join binds a fresh browse endpoint.
- **Relay setting** for the browse: read from the same setup JSON (`relay`, `relay_url`; NULL = defaults, n0);
  another setting rebinds. Bad JSON returns -1 and binds nothing.

## Checks

- `cargo test lobby_c`: listing fit/parse, browse states (binding / bind failed), a join dialling from the browse's
  endpoint (guest's `me` is the browse endpoint's ID; local DHT testnet, relays off), C ABI bad-setup.
- `lobby_c::tests::lan_lobby_heard_joined_and_unlisted` (`#[ignore]`, needs multicast, like
  `transport::lan_listing_heard_by_mdns`): host heard with `{id:"m1",name:"h",open:3}`, joined by the heard code from
  the browse endpoint, a second browse sees `open` 2 then 1 after a CPU seat, and the game is gone after the start.
  Passed 4 of 4 runs here with `cargo test -- --ignored` (this machine has multicast).
- Two processes through the C ABI (real `libpbnet.a`, scratch C program, not committed): one `pbnet_lobby_host`
  with relay off, the other `pbnet_lan_browse` until it heard `{"code":"<id>","id":"lan-c","name":"Hosty","open":3}`,
  `pbnet_lobby_join` with that code, seated, ready reached the host.
- Stub: `pbnet_lan_browse` gives a closed list with the "not available" error; compiled with `-Wall -Wextra -Werror`.

Nothing tried that failed, beyond a test bug (a tokio `sleep` built outside the runtime).

## Not verified

- In the game / menu: no N28 UI yet and no disc image (GMPE01) here, so nothing calls these from the app.
- The gate does not run the mDNS test (ignored, as N20's transport test, since CI sandboxes often lack multicast).
- LAN across two real machines, Android or iOS mDNS: only loopback/one host here (no devices, no Apple toolchain).
- The tvOS stub build itself (no Apple toolchain); the stub file compiles standalone on Linux.
