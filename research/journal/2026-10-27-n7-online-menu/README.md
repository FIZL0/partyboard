# N7: online menu (RmlUi)

## What landed

- `src/port/ui/online.cpp/.hpp`: two `Window`s built from the same parts as Settings (Pane, SelectButton, NumberButton,
  BoolButton, StringButton, Modal), so pad, keyboard, mouse and touch go through the existing nav code.
  - **OnlineWindow** (menu bar → *Online*, new tab between Settings and Achievements). Tabs:
    - *Host*: shows this device's players and relay, **Host Game** → `pbnet_lobby_host`. "Back to Lobby" if one runs.
    - *Join*: code field (short code, ticket or link), **Paste Code** (clipboard, for phones), **Join Game** →
      `pbnet_lobby_join`.
    - *LAN*: text only: the C ABI has no LAN browse yet (N24 adds it; UI follow-up filed as **N25**).
    - *Continue*: lists `<config>/online/*.json` (N9's save path) newest first, with the local save time. Picking
      one only shows "not available in this build yet": the resume path is N9 (follow-up **N26**).
    - *Players*: players on this device (1..4), their names, relay (Public `n0` / Own + URL / Off).
  - **LobbyWindow** (replaces OnlineWindow once host/join returns 1). Polls `pbnet_lobby_gen()` each frame and re-reads
    `pbnet_lobby_json`. Tabs:
    - *Lobby*: status line (going online / joining / waiting for X to be ready / everyone ready), host's code
      (press to copy; long code button for when there is no short code), 1P..4P seat list (host: press a seat for
      Open / CPU Easy..Very Hard → `pbnet_lobby_cpu`), "Your Characters" one row per local seat (Left/Right cycles
      skipping taken characters, Confirm opens a list; team buttons when Team Battle is on) → `pbnet_lobby_picks`,
      guest **Ready** toggle → `pbnet_lobby_ready`, host **Start Match** → `pbnet_lobby_start` (reason shown in a
      modal), **Leave Lobby** (also on B/close, with a confirm).
    - *Rules*: board (6 boards, Left/Right or list), turns 10..50 by 5, bonus stars, team battle, handicaps per
      seat → `pbnet_lobby_settings`. Read-only (disabled) for guests.
    - On `started`: toast "The match is starting" and the window closes **without** leaving the lobby (N8 picks the
      started room up). On `closed`: modal with pbnet's error, then back to OnlineWindow.
- `src/port/ui/online_model.cpp/.hpp`: everything without RmlUi: profile (`<config>/online.json`: names, a random
  256-bit hex key per local player made once, local count, relay, last code), the setup JSON, parsing the lobby
  snapshot, `not_ready`, `character_taken`, the save list. Kept separate so it can be driven by a plain test program.
- Build id sent to pbnet: `PARTY_BOARD_VERSION_STRING + " " + PARTY_BOARD_WC_REVISION`. Disc id from
  `version::getDiskID()` as `GMPE01-00` (gameName, company, `-`, two-digit gameVersion).
- `src/port/pbnet_stub.c`: added the `pbnet_lobby_*` functions (host/join return 0, state NONE, JSON
  `{"state":"none"}`, start says why). Before, N20 left them out (cmake only warned), and now that the menu calls
  them a PBNET_STUB build (tvOS) would fail to link.
- No new `res/` files: the existing `window.rcss` styles cover it.

## How it was checked

- `tools/check.sh` (cargo tests + full CMake build).
- Real pbnet, two processes (scratch harness, not committed: links `libpbnet.a` + `online_model.cpp`): host with
  `relay: off, short: false`, guest joins by the host's ticket with 2 local players from a profile file. The parser
  read every snapshot right: guest seated in 1P/2P, picks (characters 5, 6 and teams) arrived, ready set, host changed
  rules (board 2, 35 turns, no bonus, teams, handicaps 0..3) and put a CPU on seat 3, `pbnet_lobby_start` → both
  sides `started` with the final room. `not_ready` went 1 → 0 as expected. Note: a rule change clears the guest's
  ready (pbnet), so in the UI the Ready toggle flips back off and the guest presses it again.
- In the real app (no disc, so from the prelaunch screen with a temporary, reverted hook in `portmain.cpp` that pushed
  the windows, plus a temporary RML dump in `Window::update`): every OnlineWindow tab and both LobbyWindow tabs (with a
  real host lobby, relay off, 2 local players) built the expected content, and nothing crashed. The Continue tab listed
  a fake save and Players loaded a profile from a throwaway `XDG_DATA_HOME`, so the user's config dir wasn't touched.

## Tried and failed

- Screenshots: `grim -g <window geometry>` hung on this Hyprland session (the window is probably on a workspace you
  can't see). `SDL_VIDEO_DRIVER=offscreen` crashes aurora (no surface). The window only got about 3 frames in 10 s
  there (it looks like present blocks), so the content check dumps the DOM at the 3rd update rather than after the
  lobby reaches `open`: the lobby dump shows the static build plus the first snapshot.

## Not verified

- **What it looks like and how it navigates**: no screenshot (see above), no pad/key/touch input was injected (I
  won't type into the user's desktop session). Layout, focus order, Left/Right cycling and touch are untested by
  eye. They use the same components and Pane/Window nav as Settings, nothing custom.
- **In-game entry**: the menu bar only exists once the game runs, and that needs the GMPE01 disc (not on this
  machine). The *Online* tab was not opened from the real menu bar. `disc_id()` with a real disc was not seen
  either (without a disc it falls back to `GMPE01-00`).
- **Short codes / public relay**: the probe ran with the relay off and no short code (no internet use in tests).
  The "Code" button showing the short code and its error text are unchecked live.
- **Match start**: on `started` the menu just closes; entering the board is N8.
- **Mobile**: soft keyboard for the code and name fields, and touch: N12.
- **Stub build**: `pbnet_stub.c` was compiled alone (`gcc -Wall -Wextra`, clean). A full PBNET_STUB=ON build was
  not run.
