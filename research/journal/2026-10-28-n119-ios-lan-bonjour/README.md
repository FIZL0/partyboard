# N119: LAN lobbies on iOS (Bonjour) and DNS-SD everywhere

pbnet's LAN discovery was only `iroh-mdns-address-lookup` (swarm-discovery): raw UDP multicast on 224.0.0.251:5353.
An iOS 14+ app may only do that with Apple's restricted `com.apple.developer.networking.multicast` entitlement, which
a sideloaded re-signed IPA can't have. NSBonjourServices (`_pbn._udp`, N110) only covers the Bonjour APIs.

The task asked to check on the iPhone first. This run was unattended with no iPhone, so I did not wait for that
check. Apple documents the iOS 14 restriction, and swarm-discovery has no Bonjour path at all, so I built the Bonjour
path. A Linux stand-in tests it: Avahi's dns_sd compatibility library (`libdns_sd`) plays the part of the iPhone's
mDNSResponder.

## Why a plain "browse with Bonjour" isn't enough

swarm-discovery's records aren't DNS-SD:
- **No PTR record.** It answers a PTR query for `_pbn._udp.local` with SRV and TXT answers only. A Bonjour browse
  (`DNSServiceBrowse`, `NWBrowser`) is driven by PTR records, so an iPhone would never see a desktop host.
- **TTL 0 on every record** (`Record::from_rdata(name, 0, ..)` in its sender.rs). To a DNS-SD cache (mDNSResponder,
  Avahi), a TTL-0 record is the **goodbye** of the identical record. Our records must never be byte-identical to
  swarm-discovery's. If they were, every swarm-discovery response would delete them from the iPhone's cache.
- swarm-discovery also skips records with the cache-flush bit (its hickory build has no `mdns` feature, so class
  0x8001 is not IN), and reads SRV/TXT only from the answer section. mDNSResponder flushes its unique records and puts
  SRV/TXT in the additional section. So the desktop's iroh mDNS can't hear an iPhone either.

So both ends need standard DNS-SD: Bonjour on iOS, and our own small DNS-SD responder/listener everywhere else,
running beside iroh's mDNS (which still finds older builds).

## What changed (net/)

- `lan.rs` (new): the shared part.
  - `Book`: the endpoints heard, with their addresses, user data and relay.
  - `LanLookup`: an iroh `AddressLookup`. A bare ID resolves from the book (it waits up to 10 s, as iroh's mDNS does),
    and iroh's `publish` feeds the backend through a watch channel.
  - TXT encode/decode, and the label (`<id>` = base32 of the endpoint ID, lowercase, as iroh's mDNS).
  - `Kind` (Multicast | Bonjour) and `Dns::start`.
- `dnssd.rs` (new; every iroh build but iOS): a hand-written mDNS wire format, with name compression on read and none
  on write. hickory-proto's `mdns` feature would change swarm-discovery's parsing through feature unification.
  - It answers PTR queries with the PTR, plus SRV, TXT and A/AAAA as additionals, and answers SRV/TXT/A/AAAA queries.
    Legacy unicast queries get a reply to the sender with the same ID; a QU query gets a unicast copy as well.
  - It announces every change twice, 1 s apart. When something changes it first sends goodbyes for the old records.
  - It sends a PTR query at 0, 2, 6, 16 s and then every 10 s, and listens to every response.
  - Our socket shares the port with SO_REUSEADDR (and SO_REUSEPORT on unix), joins the group on every IPv4
    interface, and sends out each one. IPv4 only.
- `bonjour.rs` (new; iOS, and with the new `bonjour` cargo feature elsewhere, against Avahi's libdns_sd): dns_sd FFI.
  - One thread owns every `DNSServiceRef`, polls their sockets and runs the callbacks. A callback only queues work;
    refs are started and stopped after `ProcessResult` returns.
  - Register: `DNSServiceRegister`, or `DNSServiceUpdateRecord` when only the TXT changed.
  - Browse: browse → resolve (host and port) → `DNSServiceQueryRecord` for the host's A/AAAA and the instance's TXT,
    all kept running, so changes are heard.
  - A broken daemon connection (for example after a suspend) tears everything down and starts again after 2 s. A
    Local Network permission denial is logged in words.
  - FFI signatures and constants were checked against the iOS SDK's `dns_sd.h`. The symbols are in `libSystem`
    (libsystem_dnssd).
- `transport.rs`:
  - `bind_with` → `bind_lan(.., lan::Kind::default())`.
  - On iOS: no iroh mDNS, Bonjour instead. Elsewhere: iroh mDNS plus `dnssd.rs`.
  - `has_lan` = either is up. `lan()` merges both lists.
  - The old log line "no LAN lobbies on this device" now prints only when neither starts.
- `Cargo.toml`: optional `data-encoding` and `socket2` (versions iroh already builds), plus `tokio/net`, under
  `iroh`. New `bonjour` feature (`dep:libc`).

### The record

- PTR `_pbn._udp.local` → `<id>._pbn._udp.local`.
- SRV → `<id>.local:<iroh's IPv4 port>`. Not swarm's `<id>-<port>.local`, because swarm's TTL-0 records would act as
  goodbyes for ours.
- TXT: `sd=<n>`, then `relay=<url>` and `user-data=pb1<listing>` (the keys iroh's mDNS uses).
- A/AAAA of the iroh addresses on that port.
- TTL 120. SRV, TXT and A/AAAA carry the cache-flush bit.

**The TXT's `sd=<n>` is a sequence number.** It goes up on every TXT change, and readers take the TXT with the highest
number. The reason: Avahi gives a TXT query started within about a second of a change (inside the old record's
goodbye window) the *old* TXT, and then never reports its removal. Found by reproducing a lobby test failure; see
below.

## Checks

- **Unit tests** (in `cargo test`, so in check.sh):
  - wire round trip;
  - a hand-built, compressed mDNSResponder-style browse answer (PTR answer; SRV/TXT/A additionals) is heard;
  - swarm-style TTL-0 SRV/TXT records change nothing;
  - an older `sd=` never replaces a newer one;
  - goodbye removes;
  - answers for PTR/ANY/A/AAAA;
  - malformed and looping packets are rejected;
  - TXT/label codecs, `newest`, and the book's expiry and resolve.
- **Real multicast on this machine** (ignored tests; all five pass in parallel, run twice):
  - `cargo test --features bonjour --lib -- --ignored lan_ bonjour dnssd`
  - `dnssd::tests::dnssd_hears_dnssd`: two sockets hear each other, a TXT change and the goodbye.
  - `transport::tests::bonjour_and_desktop_hear_each_other`: "iPhone" (Bonjour via Avahi) host → desktop finder,
    desktop host → "iPhone" finder, and "iPhone" → "iPhone". In each case: listing heard, listing change heard, the bare ID dialled over
    the LAN (QUIC hello received), unlisted, listed again, gone after close (goodbye / deregistration). Passed 3
    times in a row on its own.
  - `lobby_c::tests::bonjour_lan_lobby_heard_and_joined`: the real lobby. Host open → heard in a LAN Browse → joined
    by the code heard → a new Browse sees one seat fewer → gone after leave. Both directions.
  - The existing `lan_listing_heard_by_mdns` and `lan_lobby_heard_joined_and_unlisted` still pass.
- `avahi-browse -rpt _pbn._udp` resolves a `dnssd.rs` endpoint fully (instance, `<id>.local`, address, port, TXT) on
  every interface. That is a third-party DNS-SD stack confirming the records.
- **Cross builds:**
  - pbnet `cargo build --release --target aarch64-apple-ios` with tools/ios-build.sh's env links. `llvm-nm` shows the
    8 `DNSService*` imports undefined, and the SDK's libSystem.tbd exports them.
  - `cargo check` for x86_64-pc-windows-gnu and aarch64-linux-android (NDK 28 clang) is clean.
  - The browser build (`--no-default-features`) doesn't compile any of it; its tests pass.
- **IPA:** `tools/ios-build.sh` + `tools/ios-ipa.sh` build `build/partyboard-ios.ipa`, and `tools/ios-ipa-check.py`
  passes; it resolves every import against the SDK. `libdol.dylib` carries the 8 `_DNSService*` symbols and the
  Bonjour code's strings. The Info.plist in the IPA keeps `NSBonjourServices` `_pbn._udp`.

## What failed on the way

1. First Bonjour run: desktop host → Bonjour finder never heard the listing.
   - Avahi's per-interface TXT events alternated between the listing TXT and the empty pre-listing TXT. The bind-time
     empty TXT and the listing TXT were less than 1 s apart, and the cache-flush rule keeps records from the last
     second, so both stayed.
   - Fix: send explicit goodbyes for changed records.
   - While there, I found that swarm-discovery's TTL-0 records were byte-identical to ours (same TXT, same SRV target
     `<id>-<port>.local`). They acted as goodbyes, so the records were made distinct.
2. The lobby test: desktop host → Bonjour guest stayed "Connecting", and on another run a fresh Browse stuck at
   `open: 3` after the change to 2.
   - A wire sniff (temporary) showed only `open: 2` being served after the goodbye of `open: 3`.
   - A temporary test reproduced it exactly: a Bonjour finder created 0 ms after a change keeps the old listing for
     the whole 10 s. At 50 ms and 500 ms it recovers; at 900 ms and 1500 ms it is fine.
   - Fix: take the TXT only from the TXT query (not from Resolve's callback), track TXT records per (data, interface),
     and add the `sd=<n>` sequence number. After that, 0 ms and 50 ms both give the new listing at once, and the
     lobby test passes in both directions.
   - The first failure ("Connecting") did not come back after these fixes. It was most likely the same stale-TXT and
     address churn, but I did not pin it down separately.
3. Running the new lobby test beside the existing LAN lobby test made both fail, because both hosted match id `"m1"`
   and each found the other's host. The new test hosts with id `"n119"`.

## Follow-ups filed

- **N126** Android: no `WifiManager.MulticastLock` is taken. Many phones drop incoming multicast without one, which would stop
  both iroh's mDNS and `dnssd.rs` from hearing anything there. This predates N119.
- **N127** the iPhone checks listed below.

## Not verified

- **Anything on a real iPhone.** The task needs the user's iPhone and this run was unattended. Specifically:
  - That raw multicast really fails there. The task's first step; skipped, I went straight to the Bonjour path.
  - That the Local Network prompt appears and that, once allowed, an iPhone host is listed on a PC and a PC host on
    the iPhone.
  - Joining in both directions, listing updates, and that suspend/resume restarts the browse (the broken-connection
    path is untested anywhere: Avahi never broke the connection).
  - The `PolicyDenied` log when Local Network is off.
  - Avahi stood in for mDNSResponder everywhere above. Their caches differ (the stale-TXT case is Avahi's behaviour;
    `sd=<n>` covers it either way).
- **iPhone ↔ iPhone on real phones.** Only Bonjour ↔ Bonjour through Avahi on one machine (the transport test's third
  case) was run.
- **Windows and Android at run time.** They only compile.
  - On Windows the socket has no SO_REUSEPORT, and the system's own mDNS (dnscache) may hold 5353 differently than on
    Linux; swarm-discovery has the same exposure.
  - Android has the missing multicast lock (filed).
- **IPv6-only LANs.** `dnssd.rs` speaks IPv4 mDNS only. iroh's IPv6 addresses on another port than the IPv4 one are
  left out of our SRV.
- **Real networks with several machines.** Every multicast test ran on one machine (loopback delivery on its
  interfaces).
- **The game's LAN tab.** No two-copy in-game run (N89's harness) of the desktop build. The Rust lobby layer under it
  (`Browse`, `LanList`, join by the heard code) is what the tests cover, and the C side is unchanged.
- **Old builds.** An iPhone can't find a desktop running a build from before N119 (no PTR record). Desktop ↔ desktop
  with old builds still works through iroh's mDNS. Not tested with an actual old binary.
