<div align="center">
  <img src="res/logo.png" alt="Logo" width="800">
</div>

# Party Board Online

A fork of [mariopartyrd/partyboard](https://github.com/mariopartyrd/partyboard) that adds online play.
Everything below "Overview" is upstream's README; the port itself is their work.

## What this fork adds

- **Online play**: delay-based lockstep (every peer runs the same frames with the same four pads) over
  [iroh](https://iroh.computer) QUIC. Join by a short code, find games on your LAN, or go through a relay
  (relay URL is a setting).
- **Lobby**: host or join from the Online menu (the launcher's **Online** button next to Play, or the menu bar),
  pick characters, fill empty seats with CPUs, ready up and start.
- **Party Board in the browser**: `tools/web.sh` builds `build/web/` (Emscripten, WebGPU through emdawnwebgpu),
  about 6.6 MB to download (brotli). No game data is in it: the player picks their disc image in the page, which keeps
  it in the browser's private storage. Checked in Chromium, Firefox and WebKit (Playwright on Linux).
  Play it at <https://fizl0.github.io/partyboard/> (`tools/web-pages.sh` publishes `build/web/` there).
- **Online play in the browser** over WebRTC (codes, join links, mid-match redial, 2-4 browsers), and **desktop and
  browser in one match**: the desktop's Host > Host for Browsers speaks the browser's WebRTC (N96).
- **Autosave and Continue**: an online match saves a checkpoint at every player's turn and after every minigame on
  every peer (`<user dir>/online/`); the Online menu's Continue tab resumes it. The save is deleted when the party
  ends. Offline parties autosave every turn too (`<user dir>/local/party.json`), offered by the game's own Continue.
- **Drops never end a match**: the game holds with "Waiting for &lt;name&gt;", a guest that restarts the app can
  rejoin (the match reloads from the last checkpoint), or the host lets a CPU play for the missing player.
- **Desync check and resync**: peers compare a gameplay-only state hash (players, system state, pads; not menus or
  effects) during boards and minigames. On a mismatch everyone resyncs to the host's newest point (the current
  turn's start or the minigame's start) and plays on.
- **On-screen touch pad** (stick, A/B/X/Y, L/R/Z, Start) on touch screens, shown on the first touch and hidden by
  a controller or key (Settings > Input > Touch Controls).
- **Per-controller button icons**: prompts show the buttons of the controller each player uses (PlayStation, Xbox,
  Switch, generic, keyboard keycaps, touch), also in the menus (Settings > Input > Button Icons).
- **Log file** on every platform (`<user dir>/logs/partyboard.log`, Settings > Advanced).
- **First launch** uses the Party Board preset directly, with no Classic / Party Board prompt.
- **Mobile**: the Rust netcode (`net/`, `pbnet`) builds for Android (Gradle APKs), and `tools/ios-ipa.sh` builds an
  **unsigned iOS IPA on Linux** (`build/partyboard-ios.ipa`) for you to sign and sideload. Released IPAs install and
  update through the FIZL0 source: add `https://fizl0.github.io/apps/source.json` in AltStore, SideStore or Feather.

**Platforms**: Linux, Windows, Android, iOS and the browser. macOS and tvOS are **not targets** of this fork:
upstream's files for them are left as they are, but nothing here is built, tested or fixed for them.

Design and hook points: [plan/NET.md](plan/NET.md). Findings per task: [research/journal/](research/journal/).

### Not done yet

This is work in progress; see [plan/TODO.md](plan/TODO.md) for the full list. Notably:

- **No real devices in testing**: no iPhone or simulator was used to check the iOS app. On a user's iPhone it
  crashes whenever iOS takes focus (the Local Network prompt, Control Center), since there is no background handling
  yet. Android ran only on the
  emulator. The browser build ran only in Playwright's browsers on Linux, not Safari on iOS/macOS, Chrome on an
  Android phone or with a real gamepad.
- **Online play has not been played on two physical devices**: two-peer matches ran as two processes on one
  machine (desktop-desktop, browser-browser in headless Chromium, desktop-browser).
- **No audio** on any platform yet.
- **Browser**: WebGPU only, there is no WebGL2 path, so a browser without WebGPU shows a message instead of the game.
  Minigame m411 aborts in the browser (negative viewport), and browser and desktop split on two boards in long
  scripted parties.
- Minigame m444 never ends in some scripted parties, and the tutorial board crashes when jumped into directly;
  both are open.
- **Windows**: the netplay hooks (the define route for MSVC) have only been built on Linux and with toy MinGW DLLs,
  never on a real Windows/MSVC machine. This fork's CI has never run.

# Overview

[Build Status]: https://github.com/mariopartyrd/partyboard/actions/workflows/build.yml/badge.svg
[actions]: https://github.com/mariopartyrd/partyboard/actions/workflows/build.yml

A work-in-progress Windows/Linux/macOS/Android/iOS port of Mario Party 4.

This repository does **not** contain any game assets or assembly whatsoever. An existing copy of the game is required.

Version Completion:

- `GMPE01_00`: Rev 0 (USA)
- `GMPE01_01`: Rev 1 (USA)

### 1. Download [Party Board](https://github.com/mariopartyrd/partyboard/releases)

Those are upstream's builds, without online play. This fork's releases ([FIZL0/partyboard](https://github.com/FIZL0/partyboard/releases),
`tools/release.sh`) carry only the unsigned iOS IPA so far: on an iPhone, add the source
`https://fizl0.github.io/apps/source.json` in AltStore, SideStore or Feather to install and update it. Other platforms:
build it from source (below).

### 2. Setup the game

- Extract the .zip file
- Launch partyboard or partyboard.exe depending on your platform.

# Building

If you'd like to build Party Board from source, please read the [build instructions](building.md).

## Common problems

### RenderDoc not working

RenderDoc has some conflict with asan. To turn off asan, you should delete the two lines in `CMakeLists.txt` that enable ASAN for the DOL and the RELs: `set_source_files_properties(..., -fsanitize=address)`

# Credits

Special thanks to the GC/Wii decompilation community, the [Aurora](https://github.com/encounter/aurora) developers, the Dusk developers, all [contributors](https://github.com/mariopartyrd/partyboard/graphs/contributors), [ImWhoreHay](https://x.com/ImWhoreHay) for the font, and justcamtro for designing the assets.

<br/>
<div align="center">
    <a href="https://github.com/encounter/aurora">
        <img src="assets/aurora-powered.png" alt="Powered by Aurora" width="800">
    </a>
</div>
