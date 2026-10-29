# N144: the desktop's LAN tab lists browser lobbies too

Before this task, the desktop's LAN tab only heard iroh mDNS. A lobby hosted in a browser (or with a desktop's Host
for Browsers) on the same Wi-Fi is WebRTC, and it only puts N139a's pkarr record under the network's public IPv4
(`net/src/nearby.rs`). So the desktop never listed it.

## What changed

- `net/src/link.rs`: `Transport::bind_nearby(relay)` = `rtc::Transport::bind_browse` with the relay setting as
  WebRTC reads it. It learns the public IPv4 from a throwaway str0m offer's srflx candidate, then reads the
  address's slots.
- `net/src/lobby_c.rs`:
  - `Browse` has a second, WebRTC browse (`nearby`). `Browse::with_nearby(bind)` binds it on the runtime. On the
    desktop (`feature = "iroh"`), `pbnet_lan_browse` adds it to every browse it makes. The browser build is unchanged,
    because its one browse already is the nearby one.
  - `Browse::list()` merges the two lists: mDNS's lobbies first (endpoint IDs), then the WebRTC ones (short codes).
    `lan` is true if either can list: mDNS is on, or the public address was learnt or is being learnt.
  - `bind_join`: a short code that the browse's WebRTC list holds is joined over WebRTC straight away. It skips
    N139's relay lookup ("is this code a WebRTC lobby?"), which saves one relay request per join. Other codes go
    through the lookup as before.
  - The WebRTC browse goes with the `Browse`. When it is dropped or taken by a join, the `rtc::Transport` and its
    `Found` drop with it, and the `nearby::browse` task ends at its next read.
- `src/port/ui/online.cpp`: the desktop LAN tab's footer says "(here, in a browser, or with Host for Browsers)".
- Test `rtc::tests::desktop_lan_tab_lists_and_joins_a_browser_lobby` (desktop build, under rtc's `SERIAL`):
  - A fake STUN server on loopback answers with a 198.18.x.y address, and a WebRTC host on `Relay::Own("stun:…")`
    lists itself on the fake pkarr relay.
  - A `Browse` with `with_nearby` lists it with `lan` true, `lists_nearby` is true, and `bind_join` from that browse
    seats the guest over WebRTC.
  - With relay Off, the nearby side has nothing to list, and there is no error: mDNS's part of the tab still works.

### Relay budget

The relay allows about 0.5 requests a second per address, shared by everyone there. A desktop's WebRTC browse costs
the same as a browser's: 3 reads at the start, then 1 every 10 s. It runs only while the LAN tab is open, because
`update()` stops the browse when the tab closes. A join from the list saves the lookup request. The STUN offer uses
no relay requests.

## Runs (desktop + one Chromium tab; the tab hosts with relay n0; `harness/`)

The harness is a copy of N139a's, with GUEST=1 as its default. To use it:

1. Run `harness/apply.py`.
2. Build the desktop. Build web with `WEB_FAST=1 tools/web.sh` (`build/web` was older than N139a and had to be
   rebuilt).
3. Run `harness/run.sh <name> 90`. Runs go to `.n144-runs/desk-<name>`.
4. To take the harness out, `git checkout src/port/netplay.c src/port/ui/online.cpp`, then put back any real
   online.cpp changes.

The desktop side waits for the tab's code. Then:

- **Menu mode (default).** It drives the real menu with RmlUi key presses: F1, Right, Enter, PageDown ×2 to reach
  the LAN tab. It presses Down until the focused button says "seats open", takes a shot (`aurora_capture_frame_ui`
  → `x/lan.png`), and presses Enter. The desktop writes its `build_id()` to `build.txt`, and the tab hosts with it.
- **`N144_ABI=1`.** It polls `pbnet_lan_browse` as `poll_lan` does, and calls `pbnet_lobby_join` with the listed
  code, as `join_lan` does.

Results:

- **ABI (`desk-guest`).**
  - The tab's code `WZHL` was in the desktop's list 3.5 s after it showed (the browse started then):
    `{"code":"WZHL","name":"Web","open":3}`, with `lan` true and the address 96.60.54.66.
  - The join went over WebRTC without a lookup, and the desktop was seated about 4 s later.
  - The match ran 5424 frames. The tab and desktop hash logs (session 1) were identical, with no desync.
- **Menu (`desk-menu`).**
  - The LAN tab showed "Web — 3 seats open" 5.5 s after the code. 2.5 s of that was opening the tab.
  - Enter joined over WebRTC, and the desktop was seated about 11 s after the tab hosted.
  - The match ran 5438 frames with identical hash logs and no desync. PASS.
  - `lan.png` shows the tab: "Games on This Network", the WEB button, and the new footer. It is not committed
    because it is a run output.

### What failed on the way

- The first menu run joined, but the host refused it: "a different version of the game (host web-pair, you 0.1.0.36
  …)". The menu sends the real build id, while the harness's tab hosted with `build: "web-pair"`. Fix: the
  `build.txt` exchange above. This was a harness problem, not a product problem.
- The first build of the desktop test called `Lobby::webrtc`, which is private. Dropped it, because the lobby sets
  its WebRTC flag from the transport anyway.

## Checks

- `cargo test`: 108 passed, 4 ignored. `cargo test --no-default-features`: 74 passed.
- `tools/check.sh` (the gate).

## Not verified

- **Desktop LAN tab with mDNS and WebRTC lobbies at once** (an iroh desktop host and a browser host on one network):
  - The merge (mDNS first, then WebRTC) is covered by the code only. No run had both kinds.
  - The mDNS test `lan_lobby_heard_joined_and_unlisted` needs multicast (`--ignored`) and was not run. Its path is
    unchanged apart from the appended list.
- **Android/iOS desktop builds** (same Rust and menu code): not built or run. On an iPhone without the multicast
  entitlement, `lan` is now true whenever the relay is on, so the tab says "No games found yet" instead of "This
  device can't look for games". That is right for browser lobbies, but the iPhone still can't hear desktop (iroh)
  lobbies. Not seen on a device.
- **Windows**: not built. No platform-specific code changed.
- **Relay setting Own with an iroh relay URL**: the WebRTC side reads that URL as a STUN server, finds no public
  address, and lists nothing from the relay (unit-tested only with Off). mDNS is unaffected. Not run in the game.
- **A busy address** (several hosts and browsers sharing the relay's ~0.5/s): not run. It needs more than 2 game
  instances, and would mostly measure 429s (see N139a).
- **Real separate machines on one Wi-Fi**: both peers ran on this PC. The public address came from real STUN
  (96.60.54.66), and the records went through the real relay.pkarr.org.
