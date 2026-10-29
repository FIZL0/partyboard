<div align="center">
  <img src="res/logo.png" alt="Logo" width="800">
</div>

# Party Board Online

A fork of [mariopartyrd/partyboard](https://github.com/mariopartyrd/partyboard) that adds online play.
Everything below "Overview" is upstream's README; the port itself is their work.

## Play

You need your own disc image of *Mario Party 4* (USA, GMPE01). **No game data is included.**

| Platform | Get it | The disc |
|---|---|---|
| **Windows** 10/11 | `partyboard-windows-x86_64.zip` from the [latest release](https://github.com/FIZL0/partyboard/releases/latest) | pick it on the home screen |
| **Linux** x86_64 | `partyboard-linux-x86_64.tar.gz` from the [latest release](https://github.com/FIZL0/partyboard/releases/latest) | pick it on the home screen |
| **iPhone / iPad** | add the source `https://fizl0.github.io/apps/source.json` in AltStore, SideStore or Feather (or sideload `partyboard-ios.ipa`) | put it in Files → On My iPhone → Party Board |
| **Browser** | <https://fizl0.github.io/partyboard/> (needs WebGPU: Chrome or Edge; Safari on iPhone not working yet) | pick it in the page; it stays in the browser |
| **Android** | build from source ([building.md](building.md)) | pick it on the home screen |

## What this fork adds

- **Online play**: delay-based lockstep (every peer runs the same frames with the same four pads) over
  [iroh](https://iroh.computer) QUIC. Join by a short code, find games on your LAN, or go through a relay
  (relay URL is a setting). The host can start without everyone ready.
- **Lobby**: host or join from the home screen's **Online** button (straight to the lobby, no boot logos) or the
  menu bar; pick characters, fill empty seats with CPUs, ready up and start.
- **Browser**: the whole game in the page (Emscripten, WebGPU), reading only the parts of the disc it needs. Online
  over WebRTC with **one code** (no answer to send back), browser lobbies on your home network in the LAN tab, and
  **desktop and browser in one match** (the desktop's Host for Browsers). `tools/web.sh` builds it,
  `tools/web-pages.sh` publishes it.
- **Autosave and Continue**: every player's turn is saved, online on every peer and offline too; the home screen's
  **Continue** resumes a party in progress. The menu bar's **Quit to Main Menu** goes back to the home screen.
- **Drops never end a match**: the game holds with "Waiting for &lt;name&gt;", a guest that restarts the app can
  rejoin (the match reloads from the last checkpoint), or the host lets a CPU play for the missing player.
- **Desync check and resync**: peers compare a gameplay-only state hash during boards and minigames; on a mismatch
  everyone resyncs to the host's newest point and plays on.
- **Audio** (MusyX), with Settings > Audio > Master Volume; on iPhone it plays with the silent switch on.
- **Home screen**: the game's art and card-select music, big buttons that work by touch, pad and mouse, sized for
  phones. **Fast Menus**, a **Safe Area** setting (on iPhone only the Dynamic Island's side is kept clear), and the
  board's autosave without the saving screen.
- **On-screen touch pad** on touch screens, **per-controller button icons**, controllers connected at any time, and a
  **log file** on every platform (`<user dir>/logs/partyboard.log`).
- **Releases** (`tools/release.sh`): Windows, Linux and the unsigned iOS IPA (built on Linux), with an
  AltStore/SideStore/Feather source.

**Platforms**: Linux, Windows, Android, iOS and the browser. macOS and tvOS are **not targets** of this fork:
upstream's files for them are left as they are, but nothing here is built, tested or fixed for them.

Design and hook points: [plan/NET.md](plan/NET.md). Findings per task: [research/journal/](research/journal/).

### Not done yet

See [plan/TODO.md](plan/TODO.md). Notably:

- **Tested by a person** so far: the iPhone app (an iPhone 15 Pro: LAN lobbies, joining and playing).
  Windows, Linux and Android builds, and the browser on real devices (Safari on iOS, Chrome on Android, a real gamepad),
  were only checked by build tests, emulators and headless browsers.
- **Browser**: WebGPU only (no WebGL2 path); a browser without WebGPU shows a message instead of the game. On Linux,
  Chrome needs `chrome://flags/#enable-unsafe-webgpu` and `#enable-vulkan`, or it runs in software (the page warns).
- **Windows**: the MSVC build route of the netplay hooks was never built on a real Windows machine (releases use
  MinGW). This fork's CI has never run.

# Overview

[Build Status]: https://github.com/mariopartyrd/partyboard/actions/workflows/build.yml/badge.svg
[actions]: https://github.com/mariopartyrd/partyboard/actions/workflows/build.yml

A work-in-progress Windows/Linux/macOS/Android/iOS port of Mario Party 4.

This repository does **not** contain any game assets or assembly whatsoever. An existing copy of the game is required.

Version Completion:

- `GMPE01_00`: Rev 0 (USA)
- `GMPE01_01`: Rev 1 (USA)

### 1. Download [Party Board](https://github.com/mariopartyrd/partyboard/releases)

Those are upstream's builds, without online play. This fork's builds are under [Play](#play) above.

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
