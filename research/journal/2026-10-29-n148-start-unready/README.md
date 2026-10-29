# N148: the lobby host can start when not everyone is ready

User 2026-10-29: "make the host able to start when not everyone is ready".

## What changed

- `net/src/lobby.rs` `Host::start_with` no longer refuses with "not everyone is ready". It still refuses a second
  start. `Room::all_ready` stays: the menu uses it to decide whether to ask first. The start is still host-only:
  a `Msg::Start` from a guest falls into the host's `_ => vec![]` arm, and the new test checks that. A guest's
  `Guest::on(Msg::Start)` never looked at its own ready flag, so every connected guest starts on the host's final
  room, ready or not. Its seat and character are what the host holds. Picks go to the host as soon as they are
  pressed (`send_picks`), so a guest still on the character pane gets the character the pane shows.
- `src/port/ui/online.cpp` Start Match → `LobbyWindow::confirm_start`. With everyone ready it starts as before.
  Otherwise it asks once, "Start without Bob and Ann ready?" (names from `not_ready`, joined as "A, B and C"),
  with **Start** / **Cancel**. The Start Match help text changed to match.
- `LobbyWindow::update`, Started branch: the host can now start while a guest has a dialog open over its lobby
  (Leave Lobby's confirm, a message). That dialog used to stay up over the match, and its Leave would have left
  the started lobby. Now every Modal above the lobby window in the document stack closes along with the lobby.
- Tests updated: the old `start(..).is_err()` "not ready" assertions now check `!all_ready()`. New test:
  `lobby::tests::host_starts_without_everyone_ready` (3 guests, one ready, one with a pick: a guest's Start is
  ignored; the host's start reaches all three with the same room and g2's pick; a late Ready changes nothing; a
  second start errs). `lobby_c::tests::started_lobby_runs_the_lockstep` (real transport, 3 peers) now starts with
  guest b not ready, and both guests run the lockstep.

## In-game checks

Harness in `harness/`. `apply.py` (run from the repo root) puts N38's A masher (`PB_N148`) into netplay.c and
`n148_harness.inc` into online.cpp. Take it out by restoring those two files, not with `git checkout online.cpp`.
The hook opens the real `LobbyWindow` and clicks its buttons (`Element::Click`): the host clicks Start Match and
then the dialog's Start. The guest picks Wario, never readies, and clicks Leave Lobby so its confirm dialog is open
when the start comes.

- `harness/run.sh r2 200`: two desktop copies (iroh, relay off, own user dirs). Host log: the dialog's text was
  `Start without Bob ready?`, Start clicked, `netplay: lobby started (epoch 0): seed 18E322D0EC56EF41` on both. The
  started room had `"ready":[]` and Bob's character 4. The guest's stack 2 s after the start: MenuBar(hidden),
  LobbyWindow(closing), no Modal. Hash logs split with N38's `split.py`, then `tools/netplay/hashdiff.py` on
  session 1: **identical: 11942 frames**. Overlays w01Dll (board) → instDll → m420dll → resultDll → w01Dll.
- `harness/run_web.sh w1 200`: desktop host for browsers (WebRTC, `PB_N148_RTC`) and one headless Chromium tab
  of this worktree's `WEB_FAST=1 tools/web.sh` build (`harness/n148_pair.mjs`, N96's pair script with the tab's
  Ready taken out). The tab logged `b: not ready: []`. The desktop's dialog said `Start without Web ready?`, Start
  was clicked, and the seed was 18E32316FBC130AB. The tab reached turn 2 with 1 minigame, and the pair script
  said PASS with 0 desync lines. Hash logs, session 1: **identical for 12089 frames**, then the tab's log ends
  (the desktop was killed a moment later, at 12091).
- `tools/check.sh`: passes (cargo test 109 + 75, CMake build).

## Tried / failed

- First run.sh: both copies aborted with "No available video device". The agent shell has no WAYLAND_DISPLAY, so
  run.sh now defaults it to wayland-1.

## Not verified

- **LAN lobbies**: not run. A LAN lobby is the same `Lobby`/`Host` code over iroh with local discovery, and only
  the way the code is found differs, but no LAN-discovered match was started over an unready guest.
- **A browser host** starting over an unready guest: not run (only a desktop host, with iroh and with WebRTC). The
  wasm build compiles the same `lobby.rs` and `online.cpp`.
- **Real input on the dialog**: the harness clicked the buttons with `Element::Click`. Pad/keyboard focus and touch
  on the new dialog were not tried. It is the same `Modal` as Leave Lobby's.
- **More than one unready guest** in-game ("Bob and Ann"): the runs had one human guest, so the dialog only
  showed one name. The "A, B and C" join was checked by reading the code only.
- **Android / iOS / Windows**: not run. There is no platform code in the change.
- The web build used for the pair run was compiled with the harness hook in (its `getenv("PB_N148")` is null in
  the browser, so it is inactive). The committed tree has no hook.
- The host's status line still says "Waiting for Bob to be ready." while a guest isn't ready. It is still true, so
  it was left as it was.
