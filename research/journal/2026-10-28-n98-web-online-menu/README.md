# N98: web online play, the rest (menu, clipboard, join links, redial, 3-4 browsers)

N92 put online play in the browser build (WebRTC under pbnet's C ABI) and drove it through the C ABI only. N98 drives
the real Online menu in the browser with key presses and the page's clipboard, adds join links, a guest's mid-match
redial, and runs three and four browser peers. Everything below is in headless Chromium 154 on one machine
(`tools/web_pair.mjs`).

## What changed

- **Mid-match redial** (`net/src/rtc.rs`). The offer a guest joined with is spent once it connected, so N92's guest
  redialled a dead code forever. Now the host sends every guest a **spare offer** of its own as soon as the guest's
  connection opens: `!redial <rtco...>` on the reliable channel (not JSON, so the lobby never sees it; the guest's
  `Peer` reader takes it before the JSON parse). The host keeps it (`Hosting::spares`, one per guest, at most 8). A
  guest's `Transport::dial` answers its host's spare instead of the code when it has one (only a transport that
  connected before has one: a fresh join answers the code). The answer goes back to the host exactly as a new
  guest's does; `take_answer` matches the shown offer or a spare (a spare only for the guest it was sent to). Once
  the guest is back, it sends `Msg::Rejoin` as on the desktop (`Resume`), and the host sends a new spare on the new
  connection. Nothing changes for iroh (the desktop): all of it is in `rtc.rs`.
- **Hold screens** (`src/port/ui/online.cpp`, `__EMSCRIPTEN__` only): a reconnecting guest's modal is titled
  *Reconnecting* and offers **Copy Answer** (once the redial's answer is made) and Leave Match; the host's *Waiting
  for ...* modal gets **Paste Answer** (reads the clipboard, `pbnet_lobby_answer`, says how it went in a toast).
- **Join links**: the host's Invite *Code* copies `https://<page>/?code=rtco...` in the browser
  (`pb_web_join_link` in `src/port/web/web_clipboard.c`). `platforms/web/pb.js` reads `?code=`, starts the game with
  the stored disc at once (or after the disc is picked, on a first visit), passes the code as `PB_JOIN_CODE`, and
  takes `code` out of the address bar (a reload must not join again with a spent offer). `update_online_match`
  opens Online > Join with it once the game runs (`OnlineWindow::join_code`). Join's Paste Code takes a link too
  (`Sig::parse` already read `...code=`).
- **The answer's STUN candidate** (`src/port/web/pbnet_rtc.js`): with relay `n0`, guests' answers had **no**
  server-reflexive candidate (`host 1, srflx 0`), hosts' offers had one. A bare page shows it too: Chromium, given an
  offer that has candidates in it, gathers the answer's host candidate and ends gathering without asking STUN;
  given the same offer without its candidate lines it gathers the srflx candidate. A guest behind a NAT needs it to
  be reachable. The answer side now sets the offer without its candidates, gathers, then adds them with
  `addIceCandidate`. Guests' answers then had `srflx 1` (runs 13, 14) and connected as before.
- A guest's lobby shows the *Answer* section only when opened while joining (a lobby opened seated, as after a match,
  has nothing to copy).
- **Harness** (`tools/web_pair.mjs`): `PAIR_TABS` 2-4 (tabs c/d on `WEB_PORT+1`: two more origins), `PAIR_MENU=1`
  (the lobby through the menu, keys only), `PAIR_DROP=S` (guest b's peer connections close S seconds into the
  match; back through the hold screens). Keys are CDP `Input.dispatchKeyEvent` (SDL takes them as the keyboard's);
  where the focus is comes from a new export `pb_web_ui_focus` (`src/port/web/web_ui_probe.cpp`: the focused RmlUi
  element's text), and `find(label)` walks Down, Up, Down, Right, Left until the focus text starts with the label.
  The clipboard: `Browser.grantPermissions` (`clipboardReadWrite`, `clipboardSanitizedWrite`) per origin and
  `Emulation.setFocusEmulationEnabled` per tab (the async clipboard needs a focused document); the clipboard is
  the browser's, shared by the tabs, as between a player's apps. Menu flow: host F1 > Online > Host Game, Enter on
  Code (copies the link); guest b Ctrl+V, Online > Join (PageDown) > Paste Code > Join Game; guest c opens the link
  on its own origin and picks its disc there (a friend's first visit); d as b; each guest Copy Answer, host Ctrl+V +
  Paste Answer; Ready and Start Match in the menu. The page's `RTCPeerConnection`s are recorded (`__pbPcs`) to close
  them for a drop and to count candidate types. Desync input logs are also copied from the user dir now.
  `cmake/web.cmake` exports `_pbnet_recover_json` and `_pb_web_ui_focus` too.

## Runs (`WEB_FAST=1` build, `node tools/web_pair.mjs $PB_DISC build/n98/runN SECS`)

| run | tabs | menu | drop | result |
|-----|------|------|------|--------|
| 1-6 | 3 | yes | 90 s | harness fixes: the Code field shows the pasted *link*; c's link was built from the first code (older offer: "That answer is for an older code", the right refusal); `find` matched a focused pane whose text contained the label; a disabled Copy Answer as the guest's first control kept the focus on the tab |
| 7 | 2 | yes | - | **PASS**: whole lobby by menu and clipboard, start from the menu, 1458 frames in lockstep, no desync |
| 8 | 3 | yes | 90 s | b dropped, its hold screen's Copy Answer worked; the host's modal has its buttons in a row (harness only went Down/Up) |
| 9 | 3 | yes | 60 s | redial **worked** (`rejoin of ...: Resume { index: 1 }`, b back at frame 596 of the drop at 591); desync at frame 3000 (b vs a and c, turn 1 player 1's start; b missed the 2977 checkpoint), resync reload, on in step |
| 10 | 4 | yes | - | all four in (b, d Paste Code; c link), host relaying between 3 guests; desync at **frame 30** (c, the link tab, vs all; inputs all zero for those 30 frames), one reload, then 2132 frames in step. **c was on d's origin** (harness bug found in run 15): they shared storage |
| 11 | 2 | yes | 40 s | redial ok (3158 frames after it); desync at 2730 (turn 1 player 0's checkpoint at 2709 on a, 2701 on b), reload |
| 12 | 2 | yes | 40 s | as 11 with `--hash-log`: redial ok; `hashdiff.py`: **heap layout first differs at frame 467** (ovl 89), state at 2615; the drop was at frame ~1395 |
| 13 | 3 | yes | 45 s | **PASS**: guests' answers with srflx now (`host 1, srflx 1`), b dropped at 546 and was back at 549, 1801 frames after it, 2350 frames, no desync |
| 14 | 3 | yes | 45 s | **PASS**, final build: drop at 496, back at 499, 1587 frames after it, 2086 frames, no desync |
| 15 | 4 | yes | 60 s | c's link pointed at d's origin (`origins[i + 1]` with c joining last): c showed "Ready On" before it pressed it (d's state). Fixed: the link tab's own origin |
| 16 | 4 | yes | 60 s | b's Ready press was lost (four tabs: a few frames a second); the harness now presses again while it says Off |
| 17 | 4 | yes | 60 s | **PASS**: four origins, all four seated by menu (b, d Paste Code, c link), started from the menu, b dropped at 322 and was back at 323, 776 frames after it, 1099 frames (~5 frames/s), no desync; every answer with a srflx candidate |

Seeds: run 9 `18E2A824121C1D01`, 10 `18E2A88A0BEE5201`, 11 `18E2A8D85F784B01`, 12 `18E2A919B2A03901`, 13
`18E2A98CDD962901`, 14 `18E2A9F55B41D001`, 17 `18E2AA9EFCEFB001`. Frame rates: ~20-25 frames/s with three tabs, ~10-15 with four (one GPU, the machine at load
~25 from other slots).

Screens: `01-host-lobby-paste-answer.jpg` (run 13: the host's lobby in the browser, three seated, the toast after
Paste Answer), `02-guest-reconnecting-copy-answer.jpg` (run 14: guest b's hold screen after the drop, its answer
copied), `03-host-waiting-paste-answer.jpg` (run 14: the host's waiting screen with Paste Answer).

Other checks: `cargo test --no-default-features` (59 tests; `lobby_over_rtc_reaches_start` now also cuts the
connection mid-match on the fake page, checks that the redial's answer is for the spare offer and not the spent code,
pastes it and sends datagrams both ways again; it fails with the spare turned off), `cargo test` (iroh, 78) and
`tools/check.sh` pass. The desktop's code paths: `rtc.rs` is only built without iroh, the menu changes are
`__EMSCRIPTEN__` only except `OnlineWindow::join_code` (unused there) and the hold modal's title moved into a
variable (same strings).

### The desyncs aren't the redial's

Runs 9, 11 and 12 desynced after a redial, which looked like the redial's fault. Run 12's per-frame hash logs say no:
the heaps' layout differed from frame 467 on, before the drop (~1395), and the state from 2615. All hashes matched for
1700-2400 frames after each redial (one every 30 frames). Run 13 dropped and redialled with no desync. So these are
N99's browser board desyncs (the heap layout splits first, then the state), and the resync mended every one. Run 12's
hash logs and input logs are what N99 asks for: `build/n98/run12/a-h.log`, `b-h.log` (the match's epoch 0 is lines
2282-4921 of a, 2490-5131 of b: `a0.log`/`b0.log`), not kept in the repo (2 MB each).

Run 10's desync at frame 30 is different: all inputs zero, c's state already different at the first hash. c was the
tab opened by the link, and (found later, run 15) the harness had opened it on d's origin, so c booted on d's user
dir (config, card saves, online profile) and disc storage; run 17, on four origins, had no such desync. Filed as **N100** before that was found, with a note that it is probably the shared storage: rerun four tabs and
close it if it doesn't show. Run 12's finding is noted on N99.

## Not verified

- **STUN across two machines or networks**: one machine only. What was checked: with relay `n0` both sides now put a
  server-reflexive candidate in their code (Google STUN answers from here); the connections between the tabs used
  host candidates. Two browsers behind two NATs, symmetric NATs (no TURN), an own STUN/TURN URL, mDNS-name
  candidates across machines: not tried (no second machine or network here).
- **Real browsers and a human at the keyboard**: CDP key events and CDP-granted clipboard permissions only. Real Chrome
  may ask for clipboard-read permission on the first Ctrl+V; Firefox allows `readText` only with a user gesture and
  may show a paste button; Safari likewise. The page also takes the `paste` event, which Firefox/Safari send for
  Ctrl+V on the page, but SDL takes the key first, so whether it arrives there wasn't checked. Mouse/touch on the
  menus wasn't used (keys only).
- **A link opened by someone else's browser**: the harness rewrites the link's origin to the guest tab's own (each
  tab is a "different browser" by origin). A link on a real host name over https: not tried.
- **Redial edge cases**: a guest that drops before its spare arrived (the first ~3 s of its connection: it redials
  the spent code and the host refuses the answer as "for an older code"); a pasted spare answer that fails to
  connect (the spare is used up; the guest keeps answering it, refused; Leave Match and Rejoin isn't possible on the
  web, below); the host dropping (every guest redials the host's spare; only guest drops were run); a drop longer
  than the time a browser keeps a dead connection "disconnected" before "failed" (the harness closes the
  connections, which the other side sees at once; a cable pulled is seen only when ICE gives up, ~30 s in Chromium).
- **Rejoin after a page reload** on the web: the rejoin record keeps the original (spent) code, and the spare offer is
  only in the closed page's memory, so Online > Join > Rejoin Match can't connect in the browser. Not built.
- 3-4 tabs for longer than ~4 minutes of match (a minigame with three or four browsers, turn 2): not run (time;
  ~5-10 frames/s with four tabs on this loaded machine). Run 17 is one four-tab run with a drop, 1099 frames.
- Release (`wasm-opt`) build: all runs used `WEB_FAST=1`.
