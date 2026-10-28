# N96: desktop and browser in one online match

Before this, a desktop peer spoke only iroh (`net/src/transport.rs`) and a browser only WebRTC (`net/src/rtc.rs`), so
they could not meet. Now the desktop speaks the browser's WebRTC as well. It uses the same codes (`rtco` offer,
`rtca` answer) and the same channels (0 reliable JSON, 1 unordered datagrams). Each lobby picks one transport.

## What changed

- **`net/src/rtc/native.rs`** (new): the page's peer connections (`rtc.rs`'s polled `js` imports) as native code, on
  str0m 0.24 (sans-IO WebRTC, `rust-crypto`, no OpenSSL), modelled on opentennis's `hst-base/src/net/rtc/native.rs`.
  `rtc.rs` itself is unchanged above the `js` layer: `Transport`, `Peer`, codes, spare offers and redial all work as
  in the browser. `js`'s implementation is chosen at build time: the page's JavaScript under emscripten, this file on
  native builds with `iroh` (desktop, Android, iOS), and `fake` for the browser build's host tests
  (`--no-default-features`).
  - Each connection has one UDP socket per IPv4 address that is up (from netdev). Those are its host candidates. If
    the machine has no other address it uses loopback, so a browser on the same machine can still connect. There is
    a reader thread per socket and a driver thread for the `Rtc`. These are plain threads, not tokio, so the backend
    runs under the desktop's multi-thread runtime and under a test's current-thread runtime alike.
  - **STUN:** the ICE JSON's `stun:` URLs (relay `n0` = Google's, `own` = the URL) are sent a binding request from
    every socket. For up to 2 s, before the description is made, each answer becomes a server-reflexive candidate,
    as a browser gathers before its SDP is final. There is no TURN, as in the browser.
  - A browser's mDNS candidates (`<uuid>.local`) are dropped. Its ICE checks still reach our host candidates, and
    str0m learns the address they come from (peer-reflexive). Both runs below used only that.
  - **DTLS roles** follow `Sig::sdp`, which writes every answer as `setup:active`. When the desktop answers a
    browser's offer, the offer is passed to str0m as `passive`, so str0m becomes the client. An answer to our own
    offer makes us the server. A code has no media id: `Sig::sdp` writes `mid:0`, and the answer to our offer gets our
    own mid back (str0m's is random, e.g. `0Ng`).
  - **RTT** (the host's input delay): str0m exposes none for data channels. The ICE checks we send are STUN binding
    requests: each one's transaction id is timed until its response comes back.
  - **Close:** the channels are reset, then the connection disconnects. The other end sees `ChannelClose` at once
    instead of waiting for the ICE timeout.
- **`net/src/link.rs`** (new, desktop): a lobby's transport, `Transport::Iroh` or `Transport::Rtc`, and `Peer`
  likewise. The lobby (`lobby_c.rs`) now imports from `crate::link` instead of `crate::transport`. That import is
  the only change to how the lobby uses it. In the browser build `link` is `rtc`.
  - The transport is WebRTC when the host's setup has `"webrtc": true` (new, `Setup::webrtc`), when a join's code is
    a browser's offer (`rtco…`, or a link with one), or when a rejoin record's code is one. Otherwise it is iroh as
    before.
  - The lobby JSON has `"webrtc": true` in a WebRTC lobby. It is left out when false, so `lobby_started.json`
    (the fixture) is unchanged. The menu lays itself out by it before the transport is bound.
  - In `keep_code`, the `#[cfg(feature = "iroh")]` branches became runtime ones (`t.is_rtc()`, `t.republish()`,
    `take_error`). A WebRTC host on the desktop renews its offer every 500 ms, as the browser does.
- **Menu** (`src/port/ui/online.cpp`): **Host > Host for Browsers** (desktop only) hosts with `"webrtc": true`. The
  browser-only parts that were behind `#ifdef __EMSCRIPTEN__` are now runtime checks on `View::webrtc`, which is
  always true in the browser:
  - the host's **Paste Answer** in the lobby, in place of Copy Long Code
  - the guest's **Copy Answer** while it connects
  - the lobby refreshing for that answer
  - the hold screens' redial: the guest's Copy Answer and the host's Paste Answer
  
  The help text says "press Ctrl+V on this page" only in the browser.
- `include/port/pbnet.h`: documents the setup's `webrtc`, the lobby JSON's `answer` and `webrtc`, and
  `pbnet_lobby_answer` on the desktop.
- `net/Cargo.toml`: `str0m` and `netdev` are optional deps of the `iroh` feature, so the browser build doesn't
  link them.

## Checks

- `cargo test` (desktop features): `rtc::tests::lobby_over_rtc_reaches_start`, which ran on the fake page before,
  now also runs here on **real str0m connections over loopback**. Host code, the guest's answer pasted, seated,
  ready, started, datagrams both ways. Then every open connection is cut (`native::cut_open`), the guest redials the
  spare offer, the host takes that answer, and datagrams flow again. 2.7 s. The browser build's tests
  (`--no-default-features`, fake page) still pass.
- New unit tests: STUN request/response parsing and `stun:` URL parsing (`rtc::native::tests::stun_addresses`), mDNS
  candidates dropped, `link::tests::rtc_codes_pick_webrtc`.
- `rtc::native::tests::stun_gathers_a_public_address` (`#[ignore]`, needs the internet: `cargo test --lib --
  --ignored stun_gathers`): relay n0's offer has a `typ srflx` candidate from stun.l.google.com. Passed once here.
- **The real game, desktop vs browser** (`harness/`, disc GMPE01-01, relay off, one machine). The desktop build with
  `harness/apply.py`'s scratch hook ran in the hidden window class. The browser build (`tools/web.sh`, WEB_FAST)
  ran in headless Chromium through `harness/n96_pair.mjs`, a copy of `tools/web_pair.mjs` with a desktop peer. The
  codes went through files. CPUs were in seats 2 and 3, and both humans mashed A (START in instDll / results) for
  150 s.

  | run | host | guest | result |
  | --- | --- | --- | --- |
  | r3 | desktop (Host for Browsers setup) | Chromium tab | answer to seated in 0.3 s; 9052 lockstep frames at ~60 fps; turn 1 to minigame 15 (ovl 24); no desync on either side; **match hash logs identical for all 9083 frames** (every field incl. heap) |
  | g1 | Chromium tab | desktop (joined with the `rtco` code, answered) | 9049 frames; into turn 2; no desync; **identical for 9084 frames** |

  The pre-match session of each `--hash-log` (menus, session 0) differs. That is expected: the two peers boot on
  their own timings. Logs: `logs/r3-*`, `logs/g1-*` (the frames go in the hash logs, which were not kept: 3 MB
  each).
- Browser vs browser, unchanged: `tools/web_pair.mjs` (2 tabs, ABI lobby, 120 s) on the final web build: PASS,
  7242 frames, no desync.
- `cargo build --release --target aarch64-linux-android` (NDK 28 clang): pbnet with str0m builds.
- `tools/check.sh`.

## How to run it again

`python3 research/journal/2026-10-28-n96-desktop-browser-webrtc/harness/apply.py` from the repo root puts the hook
into `src/port/ui/online.cpp` (an `#include` of `harness/n96_harness.inc` at `update_online_match`) and
`src/port/netplay.c` (N38's A masher under `PB_N96`). Build, run `tools/web.sh` (`WEB_FAST=1`), then
`harness/run.sh <name> [secs]` (desktop hosts) or `GUEST=1 harness/run.sh <name>` (browser hosts). Out:
`build/n96run/<name>/`. Split each `--hash-log` with N38's `split.py`, then run `tools/netplay/hashdiff.py` on
session 1. To take the hook out again: `git checkout src/port/netplay.c` and remove the three lines from
online.cpp. Don't `git checkout` online.cpp while it has other changes.

## Tried / failed

- First browser run: "b boots" timed out. The copied script finds `build/web` relative to itself, so it served
  404s. `run.sh` now passes `WEB_ROOT`.
- r2: "not everyone is ready". The hook set the CPUs at Start, after the browser had readied, and a new line-up
  takes ready back. The CPUs now go in once the guest is seated, and the browser readies once it sees 4 seats.
- The ignored STUN test first passed nothing: in tests the sockets are loopback-only, so the request never left.
  `LOOPBACK` is now a test switch, and that test turns it off.

## Not verified

- **The menu buttons themselves** (Host for Browsers, Paste Answer / Copy Answer on the desktop, the hold screens'
  redial buttons on the desktop): the runs drove the same C ABI calls (`pbnet_lobby_host` with `webrtc`,
  `pbnet_lobby_answer`, `pbnet_lobby_join` with an `rtco` code) from the hook, not by pressing the buttons. The C++
  builds for the desktop and the web, but no screenshot of the desktop lobby was taken.
- **Mid-match drop and redial between a desktop and a browser**: covered only str0m to str0m (the cargo test). Not
  run with a browser (`web_pair.mjs`'s PAIR_DROP needs its menu mode, which the desktop peer doesn't have).
- **Two machines / NAT**: both runs were on one machine (host candidates on its LAN address). STUN gathering was
  checked by the ignored test only. No cross-network match was played, and there is no TURN.
- **3–4 peers mixing desktops and browsers** (the host relays guest to guest): not run. Only 1 desktop + 1 browser.
- **Android / iOS runs**: pbnet builds for aarch64-linux-android, but no APK was built or run. iOS was not built:
  `cargo check --target aarch64-apple-ios` stops at ring's C code wanting `xcrun` (ring was already in the graph;
  N86's `tools/ios-ipa.sh` wrapper was not tried). The iOS libc pin (`=0.2.189`) was not re-checked against str0m's
  graph.
- **Firefox / Safari** as the browser: only headless Chromium.
- **Windows / macOS** desktop: no toolchains here (as in earlier tasks).
- The rest of the match: 150 s each, through the first turn and one minigame, not to the party's end.
