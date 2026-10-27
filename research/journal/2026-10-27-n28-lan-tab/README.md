# N28: Online menu LAN tab

## What landed

- `src/port/ui/online.cpp` LAN tab (was a "not available yet" text):
  - In a lobby (connecting/open): "Back to Lobby" only, no browse (the lobby took the browse's endpoint, and a
    browse call would bind another).
  - Otherwise: status line (Looking for games... / No games found yet. / Press a game to join it. / this device
    has no mDNS / pbnet's error), then one row per heard lobby: host name, "N seats open" (disabled at 0); pressing it
    calls `pbnet_lobby_join(setup, code)` with the same setup as Join (so its relay matches the browse's and pbnet
    dials from the browse endpoint), then the LobbyWindow as for Join by code.
  - `OnlineWindow::update` polls `pbnet_lan_browse` every 30 frames while the tab is open (setup from the profile,
    so a relay change in Players rebinds) and rebuilds the tab when a game comes, goes or changes name/open seats
    (`lan_key`), keeping focus on the same game (by code) or in the list.
  - Tab open/closed is tracked by the status line (`WatchedText`, a LiveText with an on-destroy callback): when the
    tab's content is cleared (other tab, window closed) `mLanOpen` drops and the next update calls `pbnet_lan_stop`.
    `~OnlineWindow` clears the content first (so the callback runs while the members exist) and stops the browse.
- `src/port/ui/online_model.cpp/.hpp`: `LanList`/`LanGame`, `parse_lan` (N24's JSON; `null` or non-object leaves
  the list as is; games without a code are dropped; open clamped 0..4), `lan_key`.

## How it was checked

All with the real game and disc (`PB_DISC`, GMPE01 rev 1), own user dir under the worktree (vsync off), a temporary
(reverted) hook in `ui::update` that pushed OnlineWindow and selected the LAN tab, and a temporary DOM/state dump
in `OnlineWindow::update`. Host: scratch C program on the real `libpbnet.a` (`pbnet_lobby_host`, relay off, player
"Hosty"), not committed.

- Listing: tab showed `Hosty — 3 seats open`, status "Press a game to join it."
- Refresh: host 1 left after 25 s → list rebuilt to "No games found yet." (once mDNS
  dropped it; delay not measured), host 2 came up → its row appeared.
- Join by pressing (Submit event dispatched on the row, as the nav Confirm does): guest lobby `open`, seated 2P
  ("Player 1"); host's room shows `['Hosty', 'Player 1', None, None]`.
  First attempt was refused by the host with "a different disc (host GMPE01-00, you GMPE01-01)": my scratch host's
  disc id was wrong, the UI showed pbnet's reason via the lobby's Closed state. Good to know: the real disc reports
  `GMPE01-01`, not the `GMPE01-00` fallback.
- Switching to the Host tab: `mLanOpen` 0, `mBrowsing` 0, list cleared (browse stopped).
- `tools/check.sh` passes.

## Tried and failed

- `Rml::Element::Click()` on the row does nothing: SelectButton's `on_pressed` listens to Submit, not click. Used
  Submit in the test hook.

## Not verified

- **By eye / real input**: no screenshot and no pad/mouse/touch input injected (same reasons as N7: the window is
  on a workspace grim can't capture, and I won't drive the user's desktop). Focus kept across a rebuild is untested
  (the test had no focused element in the list).
- **Opening from the menu bar** (Online tab) rather than the test hook.
- **Two machines / phones**: one host on loopback only; Android/iOS mDNS not run (no devices).
- **Browse in `closed` state** (bind failed): never retried while the tab stays open; reopening the tab after a
  relay change rebinds. Not exercised (binding never failed here).
- **Rejoin after a lobby closed** from the LAN tab (state Closed → browse rebinds): not run.
