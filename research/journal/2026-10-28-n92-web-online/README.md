# N92: online play in the browser build (WebRTC under pbnet's C ABI)

The web build (N87) linked the C pbnet stub. It now builds the real `pbnet` crate for `wasm32-unknown-emscripten`
without iroh, with a WebRTC transport whose peer connections live in the page. Two headless Chromium tabs host and
join a lobby and play the match in lockstep (`tools/web_pair.mjs`).

## What it is

- **`net/` feature `iroh`** (default on): `transport.rs`, `codes.rs`, `android.rs`, tokio's multi-thread runtime,
  iroh/reqwest/rustls. The web build (`cmake/pbnet.cmake`: `--no-default-features` when `EMSCRIPTEN`) leaves it off;
  `lib.rs` then makes `rtc.rs` the `transport` module (and its `codes`), with the names `lobby_c.rs` uses
  (`Transport`, `Peer<T>`, `Incoming`, `Relay`, `LanHost`, `datagram`, ...). Everything above the transport is
  shared and unchanged in kind: `lobby.rs`, `lobby_c.rs`, `lockstep*.rs`, `recover.rs`, `desync.rs`, `save.rs`.
  `Cargo.lock` is unchanged (the iroh crates are only `optional`; `--locked` still passes).
- **`net/src/rtc.rs`** (the brief's `crates/hst-base/src/net/rtc.rs` is the opentennis file it follows): as opentennis `hst-base/src/net/rtc.rs`, with no `web-sys`/`wasm-bindgen` (those target
  `wasm32-unknown-unknown`). Each connection is an `RTCPeerConnection` in `src/port/web/pbnet_rtc.js`
  (`--js-library`): two data channels negotiated up front, 0 reliable/ordered (the lobby's JSON messages, one per
  data channel message, so no length prefix), 1 unordered with no retransmits (the lockstep datagrams, same bytes as
  iroh's). The imports (`pbnet_js_rtc_*`) all return at once and never call into wasm: offers/answers, state and
  queues are polled from Rust tasks. No callbacks into wasm means nothing for Asyncify to get wrong there.
- **Codes both ways** (no server: no signalling service; STUN only with the relay setting on). The host's code is
  an offer (`rtco` + base64url of the essentials: ICE ufrag/pwd, DTLS fingerprint, UDP candidates, its peer name),
  ~124 characters. A guest joins with it; its lobby JSON then has `"answer"` (`rtca...`, ~130 characters, also the
  ufrag of the offer it answers) until it is connected. The host passes the answer to `pbnet_lobby_answer` (new in
  `pbnet.h`, stub and desktop return 0); the guest connects and is seated; the host's code becomes a fresh offer for
  the next guest (`Transport::publish_code`, asked for every 500 ms by `lobby_c`'s `keep_code`, which the desktop
  uses for its short code). Relay setting: `off` = no ICE servers (the same network only), `n0` (default) =
  `stun:stun.l.google.com:19302`, `own` = the URL as the one ICE server (a STUN or TURN URL without credentials).
  Server-reflexive candidates go in the code as plain candidates (the type only orders the checks).
- **Runtime**: no threads, so `lobby_c::rt()` is a current-thread tokio runtime (`rt`, `time`, `sync`, `macros`
  build for emscripten; checked in node first). `lobby_c::pump()` runs it: 4 × `block_on(yield_now())` (runs the
  ready tasks and the due timers, never parks), called at the top of `pbnet_lobby_state` (every frame,
  `netplay_lobby_poll`) and before and after the lockstep step in `pbnet_step` (so a frame's pads leave in that
  frame). `pump()` is a no-op with iroh. `pbnet_lockstep_finish`'s drain is a task on that runtime instead of a
  thread.
- **Menu** (`online.cpp`, all `#ifdef __EMSCRIPTEN__`): the host's Invite section has **Paste Answer** instead of
  Copy Long Code; a guest's lobby has **Copy Answer** (enabled once the answer is made; the lobby refreshes every
  frame while a guest connects, as the answer doesn't move `gen`); the code is shown shortened.
- **Clipboard**: SDL 3.4 has no Emscripten clipboard (`SDL_SetClipboardText` keeps the text in the process), so
  without help no code could leave or enter the page. `src/port/web/web_clipboard.c`: a copy also goes to
  `navigator.clipboard.writeText`; `pb.js` reads the clipboard on Ctrl+V (`navigator.clipboard.readText`: SDL takes
  the key, so the page never sees a `paste` event on the canvas) and hands it to SDL's clipboard
  (`pb_web_paste`), where Paste Code / Paste Answer read it.
- **Exports** for page scripts (`cmake/web.cmake`): `_pbnet_lobby_host/_join/_answer/_json/_ready/_start/_leave/_cpu`,
  `_pbnet_frame`, `_pbnet_waiting_on`, `_malloc`, `_free`, runtime `UTF8ToString`, `stringToNewUTF8`.
- **Two Rust staticlibs in one wasm link** (nod and pbnet, each with its own std): wasm-ld took them as they are, no
  duplicate symbols (it uses the first archive's member for each std symbol and leaves the other's out), so the
  desktop's prelink + localize (which broke the module for nod in N87) is not done on the web.

## What broke on the way

- **`File::sync_all` in `Save::write` crashed the page** at the first turn checkpoint (first `RuntimeError:
  unreachable` inside `pbnet_save_write`, later runs `null function` in `rust_oom` from a `Vec` reserve in
  `Save::write`: garbage state). The debug build (`WEB_DEBUG=1`) pointed at `<std::fs::File>::sync_all`. Emscripten's
  `fd_sync` is an async import under Asyncify (`fd_sync__async`, `libwasi.js`): on IDBFS it runs `syncfs` and sleeps
  until IndexedDB has it, and Asyncify can't unwind through Rust's std (its functions with exception handling are in
  the skip list, `tools/web_eh_list.py`). Not reproducible in a small node program (MEMFS has no `syncfs`). Fix:
  no `sync_all` on emscripten (the page saves IDBFS every 5 s). Any Rust code that may reach an async import
  (`fd_sync`, `emscripten_sleep`, ...) has the same problem; nothing else in pbnet does now.
- `_pbnet_lobby_start(uint64_t seed, ...)` from JS needs a BigInt (`0n`): wasm's i64 is BigInt in JS.
- Harness: runs 8 and 9 talked to a Chromium left over from a crashed attempt of run 7 (it still held
  127.0.0.1:9340, the new one only got [::1]:9340; a stale browser replays its old console on attach, which is how it
  showed). `web_pair.mjs` now lets Chromium pick its DevTools port (`--remote-debugging-port=0`, read from the
  profile's `DevToolsActivePort`), kills Chromium's whole process group, also on a crash, and stops if its web port
  is taken. Runs 2-7 and 10 had their own browser.
- Harness: each run's Chromium profile holds both origins' OPFS copies of the disc (1.2 GB): `/tmp`
  filled up (EDQUOT) after six runs; `web_pair.mjs` now deletes it at the end (`PAIR_KEEP_PROFILE` keeps it). Its
  web port is 8788 by default because another slot's `web_check.mjs` was on 8787/9339.
- Without `pbnet_recover_set_dir` (the Online menu sets it, the harness doesn't open the menu) desync input logs go
  to `./` (the FS root); the harness copies them out from there.

## Checks

`node tools/web_pair.mjs $PB_DISC OUT [SECS]` (after `WEB_FAST=1 tools/web.sh`): one headless Chromium (the GPU flags
of `web_check.mjs`, no background throttling), tab A on `http://127.0.0.1:8788`, tab B on `http://localhost:8788`
(two origins: separate OPFS discs and IndexedDB user dirs). Each tab picks the disc and boots to the title; A hosts
(relay off) and B joins through the exported C ABI with the codes copied between them by the script, as a player
would; A gives seats 3/4 to the computer, B readies, A starts; the match starts in both from `netplay_lobby_poll`
as from the menu. A fake standard gamepad per tab (`navigator.getGamepads`, SDL's Emscripten joystick) taps A, and
one START 300 game frames after the console shows the instDll minigame point, another at 700 if still there (a START in
its intro, or repeated ones, went to modeseldll through `omSysExitReq` in runs 5-8: wall-clock timing is wrong when
the frame rate drops). PASS: both started, both past frame 600, no
desync line.

- **Lobby over WebRTC between two tabs**: code (offer) 124 chars, answer 130 chars, answer taken, guest seated in
  ~0.3 s, the host's code a fresh offer at once (run 10: `the next guest's code is a fresh offer`); start refused once ("not everyone is ready") before the
  ready arrived, then started.
- **Lockstep match** (run 4, 420 s): 17365 frames on both peers (they stay within 2 frames), no desync: board w01
  from frame 1, every player's turn 1 (turn checkpoints and resync points written on both at the same frames: 2668,
  3322, 4269, 5199), the minigame point (frame 6675, Cliffhangers, groups 0011) and both at its instructions
  (`02-both-tabs-minigame-instructions.jpg`: A left, B right; the fake pad's glyphs). ~40 frames/s (both tabs on one GPU, the guest mostly waiting a
  frame for the host's pads).
- **A minigame and turn 2** (run 6, 433 s): turn 1 (4 turn points), the minigame point (frame 5869), minigame
  overlay 28 played to its end with both humans tapping A, resultDll (84, frame 10825), all of turn 2 (frames
  10984-14988), turn 2's minigame point (15944), 16161 frames in lockstep with no desync. It then went to modeseldll
  from instDll (the harness's START taps, timed in wall-clock seconds, came in instDll's intro or twice; the
  harness now times them in game frames), and both peers ended the match on the same frame with the same hash (`match over at frame 16161, hash
  4BE54CE66ADD74A7`, the online save deleted, the lobby back to open: run 5 ended the same way at frame 5473).
  `01-host-lobby-after-match.jpg` (run 8, the same end): the host's Online lobby in the browser after the match, with
  the code (a fresh offer, shortened) and **Paste Answer**, and the "Back in the lobby" toast.
- **Release build** (run 10, `tools/web.sh` with `wasm-opt -O2`, own browser, 1080 s): the same lobby; turn 1 (turn
  points 2644, 3693, 4656, 5571), the minigame point (7117, "Take a Breather"), both ended the match at frame 7515
  with hash 2449B14ACDCE44F1, no desync. The START at the instructions (300 frames in, the instructions up) sent
  both to modeseldll, as in runs 5-8: 5 of 6 minigame instructions in all runs did that, once (run 6) the minigame
  started. Not the harness's timing then; cause not found (instDll goes to the menu only on `omSysExitReq` or B;
  neither the system pause's quit nor netplay's match leave logged, the fake pad never presses B or Z, and SDL maps
  the pad's button 9 to START): **N97**.
- **Per-frame state equal**: run 3 with `PAIR_ARGS='--hash-log /tmp/h.log' DUMP_FILES=/tmp/h.log`: the two logs (from
  the match's frame 0) are identical in every field, heap layout included, for all 6557 frames
  (`tools/netplay/hashdiff.py`).
- **Desync recovery in the browser** (run 2, 300 s): one desync at frame 4050 (right after CPU player 3's turn start,
  match seed 18E2A0AF4FE4E301): both peers saw it, wrote input logs, the host reloaded the last checkpoint (epochs
  1, 2, 3 with the 2 s hold for a repeat), and the match went on in lockstep. Run 7 had one too, at frame 6480
  (seed 18E2A32F0FD0A101, during CPU player 4's turn 1, 534 frames after its start): one reload to player 4's
  turn point and the match went on in step to the minigame. Runs 3, 4, 5, 6 had none (other seeds; the human
  seats' taps follow the wall clock, so a seed doesn't replay a match). Run 8 (the machine at load 27 from other
  slots, ~12 frames/s) had one at frame 2250, in the board's intro before the first turn point, and run 9 one at
  2610, right after the first turn point; but runs 8 and 9 shared their Chromium with run 7's still-running game tabs
  (below), so they had four game tabs on one GPU. All four were on the board (none in a minigame), and the slow
  runs had the earliest: something may follow real time (N95). Follow-up: **N95**
  (`PAIR_SEED` reproduces the seed).
- **Checkpoints** (single tab, `web_check.mjs` with N87's scripted party): `netplay: checkpoint at frame 4151 ...
  online/local-....json` at each turn start, same reseed frames as N87's (4151, 5217, 6182, 7071).
- `cargo test` (iroh, 78 tests) and `cargo test --no-default-features` (59 tests; new: code/SDP round trips, ICE
  servers by relay setting, and `lobby_over_rtc_reaches_start`: two lobbies over an in-process fake page, code ->
  answer -> seated -> fresh code -> a second guest that leaves while waiting takes its answer down -> ready -> start
  -> datagrams both ways on the started wires). `tools/check.sh` runs
  both now.
- Desktop: `tools/check.sh` passes, and the Linux build's scripted offline party (`--script party.script --match
  0@1500 --turbo`, own user dir) writes its turn checkpoints at the same frames as before (4151, 5217, 6182). The desktop's code paths are unchanged: `pump()` is empty with iroh,
  `pbnet_step` only drops its lock before that empty call, the snapshot's `answer` is left out when empty, the menu
  changes are `__EMSCRIPTEN__`-only (the code shortening to 20 characters leaves 4-character short codes alone).

## Not verified

- **The Online menu in the browser**: the lobby was driven through the exported C ABI, not the menu. The menu's new
  buttons (Paste Answer, Copy Answer) and the clipboard bridge (Ctrl+V -> `navigator.clipboard.readText` ->
  `pb_web_paste`; copies to `navigator.clipboard.writeText`) compiled but were never pressed: driving RmlUi through
  CDP key presses wasn't set up, and headless Chromium's clipboard needs permissions granted over CDP. Real browsers
  may ask for clipboard permission (Chrome) or refuse `readText` (Firefox without a user gesture on the page).
  Follow-up: **N94** (with the items below marked N94).
- **A whole match to the end**: the furthest run (6) played turn 1, its minigame and results, and turn 2 up to its
  minigame's instructions (16161 frames); then the instructions went to the menu (N97), which ends the match the way
  a finished party does (same frame and hash on both, lobby back to open). Later turns, the party results
  (mstory3Dll) and a second match from the returned lobby weren't reached in the browser.
- **Two devices / two networks**: only two tabs of one Chromium on one machine (host candidates, relay off). STUN
  (`n0`), an own STUN/TURN URL, mDNS-name candidates across machines, NATs: not tried. No TURN by default, so two
  browsers behind symmetric NATs can't connect.
- **Browser and desktop in one match**: impossible as built (a desktop peer speaks iroh, a browser WebRTC; and N91's
  32-bit/libm divergence). A desktop-side WebRTC peer (opentennis has a native host): **N96**.
- **Mid-match drops in the browser**: a guest that loses the connection redials the same code, but the host's
  offer it answered is spent, so the redial can't connect until a new answer is pasted (the UI doesn't offer that);
  the host's hold/stand-in/save-and-quit paths work as on the desktop but weren't exercised. Rejoin after a reload of
  the page: not tried. (N94)
- **LAN browse**: no mDNS in a browser; `pbnet_lan_browse` lists nothing.
- **More than one guest**: the code rotates after each answer (tested in the Rust test); 3-4 browser peers and the
  host relaying guest to guest over WebRTC were not run. (N94)
- **Size**: the release wasm (`tools/web.sh`, `wasm-opt -O2`) is 18.8 MB, 6.96 MB gzip -9 (N87: 17.6 / 6.5 MB), so
  pbnet adds ~1.2 MB; N90 is about size. Runs 1-8 used `WEB_FAST=1` builds, run 9 the release one.
- **Other browsers**: only Chromium 154 headless.
