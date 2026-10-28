# N110: iOS/Android app lifecycle — hold the game in the background

The IPA crashed whenever iOS took the focus (the Local Network prompt, Control Center).

## The crash: aurora_end_frame without a frame

On iOS, SDL turns `applicationWillResignActive` / `sceneWillResignActive` (Control Center, any system prompt) into
`SDL_OnApplicationWillEnterBackground`, which sends **`SDL_EVENT_WINDOW_MINIMIZED`** to every window and then
`SDL_EVENT_WILL_ENTER_BACKGROUND` (SDL `src/video/SDL_video.c`, `uikit/SDL_uikitevents.m`). aurora's
`lifecycle_event_watch` (Android/Apple only) sets `g_backgrounded` on MINIMIZED, so `window::is_presentable()` is false
and `aurora_begin_frame()` returns false. `src/game/main.c` called `aurora_end_frame()` **unconditionally**, after a
game tick that pushed GX commands with no frame packet; `end_frame` drains the GX FIFO into `gfx` with no recording
frame.

Reproduced on the desktop with the old frame path (HEAD's `main.c`, no end_frame guard) by hiding the window
(`SDL_HideWindow` makes `is_paused()` true, which takes the same `begin_frame` → false path):

```
#1 std::deque<aurora::gfx::RenderPass>::size (this=0x0)
#2 aurora::gfx::get_render_target_size () at lib/gfx/common.cpp:548
#3 aurora::gx::map_logical_viewport ... #6 aurora::gx::fifo::handle_xf ... #8 aurora::gx::fifo::drain ()
#9 aurora::end_frame () at lib/aurora.cpp:292
#11 game_main () at src/game/main.c:225
```

SIGSEGV on the first frame after the hide. When a frame's first GX command reaches `current_frame_packet()` instead, it
is `CHECK(g_recordingFrame != nullptr, "No active frame packet")`, an abort; either way, a crash on every
resign-active. The same happens on the desktop with "pause on focus lost" on (focus lost → `is_paused`), and in the
browser if a hidden tab is reported as a hidden window. It was never a GPU device loss.

## What changed

- **`src/port/lifecycle.cpp`** (new, `include/port/lifecycle.h`): an SDL event watch (`SDL_AddEventWatch`; synchronous
  like the `SDL_SetEventFilter` the brief asked for, but it can't drop events and doesn't replace a filter someone else
  set) for `WILL/DID_ENTER_BACKGROUND`, `WILL/DID_ENTER_FOREGROUND`, `TERMINATING`, `LOW_MEMORY`.
  `port_lifecycle_hold()` (once per loop after `aurora_update`) is true while the app is in the background, the window
  is minimised or hidden, or aurora can't present (`aurora_can_present`: backgrounded, paused, Android surface not
  ready). It logs `holding the game: <why>` / `resuming the game after N held loops (ms)`.
  - Background → the settings are saved (`config::Save`, on the game thread); TERMINATING saves them at once.
  - **Autosave**: no new board write on background. N47's autosave is written (temp + rename) at every turn's start,
    and its continue path resumes *the start of* the saved player's turn (N46), so a mid-turn snapshot would resume a
    half-done turn as a fresh one. A kill in the background loses at most the current turn, as a crash does.
- **`src/game/main.c`**: `if (port_lifecycle_hold()) { frame_limiter(); continue; }` before the netplay tick gate: no
  tick, no aurora frame, no GPU work. The tick's `aurora_begin_frame()` became `aurora_begin_frame_always(&framed)`: a
  frame that turned unshowable since the check (Android flips its flags from the activity thread) is recorded and
  dropped instead of crashing; only a frame that can't be begun at all (device lost: no staging buffer) skips the tick.
  `portmain.cpp`'s pre-launch UI loop holds the same way (`SDL_Delay(16)`).
- **aurora patch `cmake/patches/aurora/0010-app-lifecycle.patch`**:
  - `lifecycle_event_watch` also takes `WILL/DID_ENTER_BACKGROUND` → backgrounded, `DID_ENTER_FOREGROUND` → not.
  - `aurora_can_present()`: presentable and not paused; when not presentable it releases the surface as
    `begin_frame` does (waits for the render thread, unconfigures), so the surface is made again on the first frame
    back (`begin_frame` → `refresh_surface(true)`).
  - `aurora_begin_frame_always(bool *presented)`: begins the frame whenever one can be recorded; when it can't be shown
    `aurora_end_frame` drains the FIFO into it and ends it with no submit and no present.
  - `aurora_end_frame()` with no frame begun: clears the FIFO, closes imgui's frame if one was opened, returns
    (the crash above, for any other caller).
  - `begin_frame` with a surface that can't be made: still fails for `aurora_begin_frame` (unchanged), dropped frame
    for `_always`.
- **Netplay: the keeper** (`net/src/keeper.rs`, as opentennis `crates/hst-base/src/net/keeper.rs`, B162):
  - `pbnet_keeper_hidden(1/0)` when the hold starts/ends; the lockstep datagrams carry a flags byte at the end
    (`FLAG_HIDDEN`; an older build's datagram has none and reads as 0, tested) and `Session::hidden(p)` keeps the
    peer's last flag.
  - `pbnet_keeper_frame()` each held loop: a keep-alive (`Session::keep_alive`: pump + send, no pads taken) at most
    every 250 ms; a keeper thread (native builds) sends one every second while the game's loop itself is stopped.
  - The lifecycle watch calls `pbnet_keeper_hidden(1)` from the background event itself: Android blocks the game
    thread inside SDL's event pump from onPause to onResume, so the game loop may never see the hold.
  - Peers: `recover::waiting_or_hidden` lists a hidden peer however lately heard (`Waiting.hidden`), and the hold modal
    says `Waiting for <name>: their game is in the background`. Nothing gives up on its own, as before (N10).
- **`platforms/ios/Info.plist.in`**: `NSBonjourServices` = `_pbn._udp` (transport.rs `MDNS = "pbn"`).

## Checks

All on Linux (Vulkan), `PB_DISC`, own user dirs. `PB_LIFECYCLE_TEST="action@loop,..."` (lifecycle.cpp) pushes the app
events / hides the window at loop counts; a 20 Hz SDL timer wakes `aurora_update`, which sleeps in `SDL_WaitEvent`
while the window is paused (hidden) — on a phone the foreground event wakes it.

1. **Offline hold, state untouched**: `mash-a.script` cut to `end 2400`, `--turbo --hash-log`, with
   `bg@400,fg@700,hide@1000,show@1100,min@1400,restore@1600`: held 300 loops (4985 ms) in the background and 100 loops
   (4953 ms) hidden, then the run finished all 2400 frames; `hashdiff.py` against the same script without the test:
   `identical: 2400 frames`.
2. **Crash before the fix**: see above (HEAD's main.c + the end_frame guard off, `hide@600`): SIGSEGV, core backtrace.
3. **Online match, guest backgrounded** (`harness/run.sh`, N89's two copies through the lobby + a `recover` command
   printing `pbnet_recover_json`): guest `bg@1500,fg@2300` (13.3 s held): host at lockstep frame 810
   `"waiting":[{"name":"Guesty","quiet_ms":66,"hidden":true}]`, `"message":"Waiting for Guesty: their game is in the
   background"`; after the guest came back the match ran on to frame 2488 on both, no desync, empty waiting list.
4. **Game loop stopped too** (keeper thread): guest `bg@1500,stall@1520,fg@1600` (game thread asleep 8 s while held):
   host saw `quiet_ms` 525 and 17 with `hidden:true` during the stall (silence would read > 3000).
5. **Android-style pause** (`pause@1500`: bg, 8 s asleep, fg inside one loop, so the loop never holds): host saw
   `hidden:true, quiet_ms 883` during it and an empty list after.
6. **Network only after Online**: an `LD_PRELOAD` shim logging `socket(AF_INET/AF_INET6)`: a scripted boot through the
   title and the mode menu (1500 frames) opened none; launcher → Online (Players tab) none; the first IP sockets come at
   Host Game. So on the desktop nothing local-network happens before the player uses Online (the LAN tab browses,
   Host/Join bind). On the iPhone the prompt should come at the same point; nothing at boot in src/ or net/ binds a
   socket (pbnet's endpoint is made by `pbnet_lobby_host/join/lan_browse` only).
7. Builds: Linux, `tools/ios-ipa.sh` (IPA 28 MB, `tools/ios-ipa-check.py` passes; Info.plist has
   `NSBonjourServices ['_pbn._udp']`; libdol exports `port_lifecycle_hold`, `pbnet_keeper_*`), Android arm64
   (`cmake --build build/android-arm64`), browser (`WEB_FAST=1 tools/web.sh`, and `tools/web_check.mjs` ran the
   2400-frame script in headless Chromium: `identical: 2400 frames` against the desktop), `cargo check
   --no-default-features` (also for wasm32-unknown-emscripten), `cargo test` (92: 89 + 3 ignored).

## Harness

`harness/`: N89's two-copy lobby harness (`n89_test.inc`, plus a `recover` command), `host.txt`/`guest.txt` and
`run.sh <name> [PB_LIFECYCLE_TEST steps]` (logs in `build/n110-work/<name>/`, or `$N110_WORK`). It needs the hook in
`src/port/ui/ui.cpp`, not committed: at the top `#include <fstream>`, `<thread>`, `<nlohmann/json.hpp>`,
`"online.hpp"` and `extern "C" { #include "game/gamework_data.h" }`; before `void update() noexcept`
`#include "port/main.h"` and `#include "<worktree>/research/journal/2026-10-28-n110-app-lifecycle/harness/n89_test.inc"`;
first line of `update()`: `n88_tick();`.

## Tried and dropped

- Pushing test steps by loop count alone: `aurora_update` blocks in `SDL_WaitEvent` while the window is hidden, so the
  `show` step never came (the loop never turned); hence the wake timer in test mode.
- Desktop minimise on Wayland (Hyprland): `SDL_MinimizeWindow` never sets `SDL_WINDOW_MINIMIZED` (xdg-shell has no
  minimised state to report back), so `min@`/`restore@` did nothing there. The check is the same flag test as hidden.

## Not done (follow-ups)

- **Surviving a Dawn device loss** (N114): the lost callback still FATALs. Recreating the device means rebuilding
  every GPU object aurora and RmlUi hold (gfx buffers/pipelines/bind groups, gx texture caches and loaded slots,
  imgui, RmlUi's render manager without dropping the port's documents): a task of its own, testable on Linux with
  `Device::ForceLoss`. With no GPU work submitted while backgrounded, iOS has no reason left to kill the app or lose
  the device for this.
- **iOS LAN lobbies** (N115): iroh's mDNS is raw UDP multicast, which iOS 14+ only allows with the restricted
  `com.apple.developer.networking.multicast` entitlement; `NSBonjourServices` covers only the Bonjour APIs. LAN lobbies
  on iOS likely need Network.framework's NWBrowser.

## Not verified

- **The iPhone itself** (no device here): that Control Center / the Local Network prompt / the home button no longer
  crash, that the game resumes with a fresh surface, and when the prompt appears. Needs the user's run of this IPA;
  N109's log file (`Documents/logs/partyboard.log`, once N109 lands) shows `partyboard::lifecycle` lines:
  `will enter background`, `holding the game: the app is in the background`, `did enter foreground`, `resuming the
  game after ...`.
- **Android on a device or emulator**: only the arm64 build. The emulator AVD (hst34) is shared by the parallel slots,
  so it wasn't started. SDL3 sends Android's onPause/onResume as the same app events (and MINIMIZED/RESTORED), which
  the desktop `pause` / `bg` steps exercised.
- Desktop minimise on X11/Windows (only Wayland here, where it isn't reported).
- A browser tab hidden mid-game (the web check ran unhidden).
