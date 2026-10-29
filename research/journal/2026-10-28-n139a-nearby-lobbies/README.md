# N139a: nearby lobbies in the browser's LAN tab

(The first session parked this task with `--after N139`, because N139's `net/src/signal.rs` wasn't on main yet. It
reopened once N139 merged, and this session did the work.)

## What changed

- `net/src/nearby.rs` (new, every build). A page has no mDNS, so "this network" means "this public IPv4 address".
  - **Address**: the first IPv4 candidate of a WebRTC description that is not private, loopback, link-local or
    100.64/10 (`public_v4`). In a browser that is the srflx candidate from the relay setting's STUN server (host
    candidates are `<uuid>.local`). On the desktop (str0m) it is the srflx, or a host address that is already public.
  - **Records**: pkarr records through N139's `signal::get/put` (same relay, same retry on 429 / failed fetch), named
    `n/<ip as 8 hex>/<slot>` with 3 slots. That name stays inside the 16 name bytes `signal::key` keeps, and can't
    collide with a 4-symbol code or `<code>/a`. A record is the lobby's LAN listing JSON (`lobby_c::Listing`: match
    id, host name, open seats) plus `"code"`, the N139 short code.
  - **Host** (`nearby::host`): once it has the short code, the listing and its address, it takes the first slot that
    is empty, stale, unreadable or its own. It puts the record again every 30 s, and on every other put first checks
    that the slot is still its own (two hosts may have taken it at once). When the lobby starts or closes it puts an
    empty record. A failed put is retried after 15 s.
  - **Browse** (`nearby::browse`): learns its own address with a throwaway offer. It reads every slot once, then one
    slot every 10 s, cycling over the slots in use plus the next one. A record counts if it was written less than 60 s
    ago, checked when it is read and again when it is listed, so a host whose page died drops out 30-60 s after its
    last put even if nobody reads its slot.
- `net/src/rtc.rs`: `Transport::bind_browse` (a transport with a browse task, used by `pbnet_lan_browse`).
  `has_lan()` is true unless the address couldn't be learnt. `lan()` gives the found lobbies as `LanHost { id: short
  code, listing }` (`LanHost.id` is now a `String`: the browser's LAN code is the short code). `set_lan_listing`
  now stores the listing, which it ignored before. `ready()` starts `nearby_host` next to `relay_host` (relay setting
  not Off). `dial` stops the browse when a join takes its endpoint. Fake page: an ICE server `stun:<IPv4>:port`
  gives an srflx candidate with that address. The new test `nearby_lobby_listed_and_joined` covers host listed →
  joined from the list → open seats follow → host leaves → gone, and no STUN → no list.
- `net/src/link.rs`: `bind_browse` = iroh `bind` (the desktop's LAN tab is unchanged: mDNS).
- `net/src/lobby_c.rs`: `pbnet_lan_browse` binds with `bind_browse`. It also **pumps** the browser's runtime: the
  browse is a task, and the runtime only runs inside per-frame C calls. In the menu `poll_lan` calls
  `pbnet_lobby_state` first anyway; the ABI harness didn't, and the task never ran. `bind_join` no longer asks the
  relay whether a short code is WebRTC in the browser, where every lobby is WebRTC (one request less per join).
- `net/src/signal.rs`: `POLL` (the host's answer poll) went from 2 s to 3 s, see the rate limit below.
- `src/port/ui/online.cpp`: the browser's LAN tab title is "Games on Your Network". Its footer says lobbies hosted in
  a browser or with Host for Browsers show up, that games are found by the network's internet address, and that on
  mobile data strangers' games may show up too. With no address the status line says "Can't find your network's
  address (Relay is Off, or no internet)". The Host for Browsers help says browsers on the network see it in their
  LAN tab. The desktop's texts are unchanged.
- `cmake/web.cmake`: `_pbnet_lan_browse` is exported for the page's scripts.
- `tools/web_pair.mjs`: `PAIR_NEARBY=1`. ABI lobby: b waits for the host's code in `pbnet_lan_browse`. The host then
  leaves, and the run times how long the code stays listed. The host opens a new lobby, and the guests join the code
  they found in the list. With `PAIR_MENU=1`: Online > LAN (PageDown twice), the button whose text says "seats open",
  Enter, plus a `b-lan.png` shot. The menu's wait for the host's short code went from 30 s to 90 s, and the ABI
  wait from 30 s to 60 s; the relay's PUTs were slow that evening, and both timed out once.
- `harness/`: a copy of N139's desktop+browser harness. `run.sh` sets `PAIR_NEARBY=1` by default, and the tab finds
  the desktop's lobby in its LAN list, checks that it is the code the desktop wrote, and joins from the list. Use it
  as N139 says: `apply.py`, build, `run.sh <name>`, then `git checkout src/port/netplay.c src/port/ui/online.cpp`.
  Runs go to `.n139a-runs/desk-<name>`.

## The relay's budget is the limit

relay.pkarr.org allows about 0.5 requests a second per address after a burst of 10 (N139's measurements). Every
tab and game behind one address shares that, which is the whole point of this feature. N139's host polled for answers
every 2 s, which is that whole rate by itself. In the first passing ABI run (2 s poll, browse every 8 s, record every
20 s), b's join took 58 s:

- b put its answer at 88 s, and its 20 s attempt (`ATTEMPT`) ran out at 108.1 s.
- The host read that answer at 108.3 s, after 429s ("Failed to fetch": the 429 has no CORS headers).
- b answered again, and the host refused that answer as "for an older code", because the offer had already been taken.
- The third round connected at 142 s.

Since then the host polls every 3 s (0.33/s), a host's record costs about 0.05/s (a put every 30 s, a read every
60 s), and a browse 0.1/s. With the code's 60 s keep-alive that adds up to about the refill rate, so the burst is
left for joins. After that change the menu join took 7 s and the desktop one 7 s. An address with more hosts or
browsers still runs into 429s and slower joins (`signal.rs` waits and asks again). An own pkarr relay would remove
the limit (not done: `signal.rs` always uses relay.pkarr.org, see N139's Not verified).

## What failed on the way

- The ABI harness's first nearby runs: b listed nothing. `_pbnet_lan_browse` wasn't exported, and the harness's
  `until` swallowed the exception. Once it was exported, the browse task still never ran, because nothing pumped the
  runtime (`pbnet_lan_browse` now pumps).
- One run's tab b stayed a blank page and never booted ("timed out: b boots"). It didn't happen again and looks like
  a harness/Chromium hiccup; this change has nothing to do with booting.
- The desktop run's first try: my edit to `n96_pair.mjs` declared `let join`, which shadowed `path.join` (TDZ), so
  reading code.txt threw inside `until`. Renamed.

## Checks run

- `cargo test` (107 passed, 4 ignored) and `--no-default-features`
  (74). New tests: `nearby::tests::*` (names, addresses, records, two hosts in two slots listed and unlisted, stale
  records, no address) and `rtc::tests::nearby_lobby_listed_and_joined` (the fake page, the whole lobby).
- Two tabs, ABI lobby (`PAIR_NEARBY=1 PAIR_RELAY=n0`, `.n139a-runs/abi-nearby`): the code `V9H8` was in b's list
  3.5 s after it showed, and gone 25.2 s after the host left (11.0 s in an earlier run). The new lobby `3F2Q` was
  listed 6 s later; b joined it from the list and was seated with nothing sent back. 7241 frames, no desync, PASS.
  This was the 58 s join above, before the poll change.
- Two tabs through the menu (`PAIR_MENU=1 PAIR_NEARBY=1`, `.n139a-runs/menu-nearby`, after the poll change): b's LAN
  tab showed `[Player 1 3 seats open]` 4.0 s after it opened, the host's log says `listed ... slot 0`, Enter seated b
  7 s later, the host unlisted at the start, 7605 frames, no desync, PASS. `b-lan.png` shows "Games on Your Network",
  the game and the footer.
- Desktop Host for Browsers + a tab (`harness/run.sh host`, `.n139a-runs/desk-host`): the desktop listed `QVAE`
  (`96.60.54.66, slot 0`, its str0m srflx), and b's list had `{"code":"QVAE","name":"Desk","open":3}` 3.8 s after
  the code showed. b joined from the list and was seated 7 s later, the desktop unlisted at the start, 7252 frames,
  no desync, PASS.
- `tools/check.sh` (the gate) after the Rust changes.

## Not verified

- **A host whose page or app dies without unlisting**, end to end: only the unit test (`stale_records_drop_out`,
  and `games()` filtering by age) and the code's reasoning (gone 30-60 s after its last put). No run killed a host
  page. Every run's host left through the lobby, which empties its record.
- **Two or three hosts on one address in a real browser**, and the slot race between them: unit test only
  (`hosts_listed_and_unlisted`, two hosts on the fake relay). A real run needs 3+ game instances, and with the
  relay's budget it would mostly measure 429s.
- **Real phones, mobile data / CGNAT strangers, IPv6-only networks**: no device run. On an IPv6-only network (or a
  STUN server reached over IPv6 only) there is no IPv4 srflx, so the tab says it can't find the address.
- **Relay setting Own (an own STUN URL) in a real browser**: only the fake page's `stun:<IPv4>` test. Relay Off:
  unit test (no STUN → `has_lan` false), not seen in the menu.
- **Desktop + tab with `GUEST=1`** (tab hosts, desktop joins): not run. The desktop's own LAN tab doesn't list
  browser lobbies at all (mDNS only), and that is follow-up N141.
- **The 3 s answer poll's effect on N139's own flows** (redial by short code, `PAIR_FREEZE`): not rerun. It adds
  about 0.5 s to the average time before a host reads an answer.
- **Windows, Android, iOS builds**: not built. No platform code or Cargo features changed: `nearby.rs` uses only
  what `signal.rs` and `rtc.rs` already build everywhere.
