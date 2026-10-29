# N139: browser join with one code, no answer to send back

## What changed

- `net/src/signal.rs` (new, every build): WebRTC signalling through the pkarr relay `relay.pkarr.org`, which the
  desktop's short codes already use (`codes.rs`). Records are signed BEP 44 / pkarr packets (ed25519-dalek, which
  iroh already built; now also in the browser build) under `pb-lobby-code/1/<code>`. That is the same key as
  `codes.rs`'s `code_key` (a test checks this against iroh's `SecretKey`), so one 4-symbol code finds either kind of
  lobby, and the record says which one (iroh ticket or `rtco…` offer). The answer goes under
  `pb-lobby-code/1/<code>/a`. HTTP is reqwest natively (`codes::client`, now `pub(crate)`), and the page's `fetch` in
  the browser (new `pbnet_js_http_*` imports in `src/port/web/pbnet_rtc.js`, started and polled like the rtc ones,
  with no callbacks into wasm). Tests use an in-process fake relay, with a per-thread "down" switch for the fallback.
- `net/src/rtc.rs`, host: with the relay setting not Off, `ready()` starts `relay_host`. It rolls a free short code
  and puts the current offer under it (again when a guest has taken it, and every 60 s), and polls the answer key
  every 2 s. Each new answer (by seq) goes through the same path as a pasted one (`take_answer_in`), spares included,
  so a mid-match redial also needs nothing pasted. `publish_code` returns the short code. While the first put is in
  flight it returns nothing for up to 10 s ("..."), and the long offer if the relay fails. A failed keep-alive keeps
  the code for up to 120 s, because the record stays fresh for 150 s. The lobby's `ticket` (the "Copy Long Code"
  button) is now the current offer.
- `net/src/rtc.rs`, guest: `dial` with a short code (`dial_short`) gets the offer, answers it and puts the answer on
  the relay. If the connection doesn't open within 20 s of time the page *ran* (`Conn::open` now counts a step longer
  than 1 s as 1 s, so a frozen page's minute counts as one second), it gets the host's current offer and answers again,
  until 600 s. If the relay won't take the answer, the answer is shown for Copy Answer as before.
- Desktop guest: `lobby_c::bind_join` asks the relay (8 s cap) whether a short code's record is a WebRTC offer, and
  binds WebRTC then. Otherwise it binds iroh as before. Join and rejoin both use it. `rtc::codes::code_in` now also
  takes short codes (the browser's lobby normalises with it). The desktop's "is this a browser's offer" checks use
  the new `offer_in`.
- `OPEN_WAIT` (how long a host waits for a taken answer to connect) went from 30 s to 90 s.
- UI (`src/port/ui/online.cpp`): help texts for the one-code flow. A WebRTC host now has Copy Long Code too, and Paste
  Answer is described as long-code only. The relay error shows under Code. The hold screen's empty-answer text is
  "Reconnecting...".
- `tools/web_pair.mjs`: `PAIR_RELAY` (the ABI lobby's relay, default off as before), short-code joins with no answer
  step, `PAIR_FREEZE=S` (guest b's page frozen with CDP `Page.setWebLifecycleState` once its answer is on the relay)
  and `PAIR_BLOCK_RELAY=1` (`Network.setBlockedURLs` on relay.pkarr.org: the fallback).
- `harness/`: N96's desktop+browser harness copied, with `N139_RELAY` (default n0) and short codes. Run it as N96
  says: `apply.py`, build, `run.sh <name> [SECS]` (`GUEST=1` for the other direction), then
  `git checkout src/port/netplay.c src/port/ui/online.cpp`. **Commit your own `online.cpp` changes first**: the
  checkout throws them away. Runs go to `.n139-runs/desk-<name>`.

## The relay's limits (measured 2026-10-28)

- CORS is fine: GET and PUT preflight both answer with `access-control-allow-origin: <origin>`.
- Rate limit per address: a burst of 10 (`x-ratelimit-limit: 10`), then about 1 request per 2 s, with 429 and
  `retry-after` beyond that. Every tab and game on one address shares it. That is why the host polls every 2 s, not
  the 1 s the brief suggested: at 1 s a host alone would hit 429 after about 20 s.
- PUT takes 0.8 to 8.5 s (the relay waits for its DHT put). GET takes about 0.35 s. One keep-alive PUT in a run took
  over 25 s, which is why `REQUEST_CAP` is now 40 s and a failed keep-alive keeps the code.
- The 429 has **no CORS headers** (checked with curl and an `Origin`: the 200s carry `access-control-allow-origin`,
  the 429s don't), so in a browser a rate-limited request is a failed fetch ("TypeError: Failed to fetch", seen once
  at a host's start in `menu-drop`). `signal::get`/`put` therefore retry failed requests as they retry 429s (2 s,
  then 4 s). A relay that really is unreachable costs 6 s more before the fallback.
- Probe scripts: `relay_probe.py`, `relay_probe2.py` (Python `cryptography`, outside the repo; not kept). The same
  thing is easy to rebuild from `signal.rs`.

## Why the guest timed out in the background (the user's report)

Before N139, the guest pressed Copy Answer and then switched apps to send the answer. Once the host pasted it, the
host gave the connection `OPEN_WAIT` = 30 s of wall-clock time. A phone's background page is frozen: no frames, so
pbnet isn't pumped. On iOS the whole WebContent process is suspended, ICE included. A guest that came back after more
than 30 s found the host had already dropped it ("A player's connection timed out"). The guest's own wait
(`ANSWER_WAIT` 600 s) was not the problem.

In headless Chromium, a page frozen with CDP still let the connection open: the host took b's answer and connected
while b was frozen (its log says "a guest's answer came by the short code" at the second it was frozen). Chromium's
WebRTC stack runs outside the page's frozen task queues. The lobby's Hello went out once b woke up.

The fixes: with one code there is no app switch in the join at all. The host waits 90 s. The guest's attempt timer
counts only time the page ran, so it doesn't give up the moment it wakes. A connection that did fail is answered
again from the relay's current offer.

## Checks run (all PASS)

- `cargo test` in `net/` (102 passed, 4 ignored) and `--no-default-features` (68 passed). New tests:
  `signal::tests::*`, `rtc::tests::lobby_by_short_code` (two guests by short code, nothing pasted, the code stays
  the same, unknown code → "No open lobby"), `short_code_falls_back_to_answers` (relay down: offer code, paste),
  `desktop_joins_a_short_code_over_webrtc` (`bind_join` picks WebRTC). Hosting tests now run one at a time
  (`SERIAL`): the process has a single `HOSTING`, and parallel tests were overwriting it.
- Two browser tabs, ABI lobby, `PAIR_RELAY=n0`: code `KPXS`, b seated 6.5 s after joining with nothing sent back,
  match 3600 frames, no desync (`.n139-runs/abi-short`).
- Two tabs through the menu (`PAIR_MENU=1 PAIR_FREEZE=60`): the invite link `?code=8RE4`, b frozen 60 s after its
  answer was put, seated when it woke, the host showed no error, match ran with no desync. Run twice, the second time
  after the "..." change (`menu-freeze`, `menu-freeze2`).
- Desktop hosts for browsers (n0), browser joins with `UGVJ`: the host log says "a guest's answer came by the short
  code"; 3621 frames, PASS (`desk-host`).
- Browser hosts, desktop joins with `8J3R`: the desktop picked WebRTC from the relay record, "answer put on the code
  server", seated with nothing sent back, PASS (`desk-guest`).
- Mid-match redial by short code (`PAIR_MENU=1 PAIR_DROP=20`, `web_pair.mjs`'s drop step now has a short-code
  branch): b closed its connections at frame 1598 and was back by itself 7.7 s later. It answered the host's spare
  offer through the relay; the host logged "a guest's answer came by the short code" and "rejoin of …". Nothing was
  copied or pasted, 3186 frames after it, PASS (`menu-drop`).
- Fallbacks: desktop host with relay Off gives a 140-char offer and the answer taken from the file, PASS
  (`desk-off`). Browser menu with relay.pkarr.org blocked: long `?code=rtco…` link, Copy Answer / Paste Answer, the
  lobby error says why, PASS (`menu-blocked`).

## Not verified

- **Real phones** (Android Chrome, iOS Safari switching apps during a join): no device or emulator run in this task.
  Only Chromium's CDP freeze was tested. iOS suspends more than that (likely ICE too), so there the path that matters
  is the guest's re-answer after a failed attempt. That path is covered only by its code and the unit tests' normal
  flow. Nothing forced a failed attempt end to end.
- **A guest frozen before its answer was put** (switching apps right after Join): not run. It should simply carry on
  when it wakes, because nothing times out while frozen.
- **3-4 browser tabs by short code** (`PAIR_TABS=3/4`): not run (the AGENTS.md instance limit, and time). The second
  guest by the same code is covered by `lobby_by_short_code` only.
- **Redial between a desktop and a browser by short code**: only browser↔browser was dropped (`menu-drop`).
- **The retry of failed fetches** (added after `menu-drop`): only the unit tests ran after it, plus the
  `check.sh` gate. No browser run, because the change only affects the error path.
- **An own `code_server`** (the setting `codes.rs` asks first): `signal.rs` always uses relay.pkarr.org.
- **The relay's rate limit with several hosts on one address**: two peers per run only. Each host's 2 s poll plus a
  60 s keep-alive is about 0.5 requests a second, the relay's whole refill rate, so two hosts on one address will see
  429s and slower joins.
- **Windows, Android, iOS builds**: not built in this task (no Cargo features or platform code changed; reqwest was
  already in those builds).
