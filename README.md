<div align="center">
  <img src="res/logo.png" alt="Logo" width="800">

  <p align="center">
    <a href="https://discord.gg/T4faGveujK">Discord</a>
  </p>
</div>

# Party Board Online

A fork of [mariopartyrd/partyboard](https://github.com/mariopartyrd/partyboard) that adds online play.
Everything below "Overview" is upstream's README; the port itself is their work.

## What this fork adds

- **Online play**: delay-based lockstep (every peer runs the same frames with the same four pads) over
  [iroh](https://iroh.computer) QUIC. Join by a short code, find games on your LAN, or go through a relay
  (relay URL is a setting).
- **Lobby**: host or join from the Online menu, pick characters, fill empty seats with CPUs, ready up and start.
- **Autosave and Continue**: an online match saves a checkpoint after every minigame on every peer
  (`<user dir>/online/`); the Online menu's Continue tab resumes it. The save is deleted when the party ends.
- **Drops never end a match**: the game holds with "Waiting for &lt;name&gt;", a guest that restarts the app can
  rejoin (the match reloads from the last checkpoint), or the host lets a CPU play for the missing player.
- **Desync recovery**: on a state-hash mismatch both sides write their input logs and the host reloads everyone
  from the last checkpoint (at most 3 times per checkpoint).
- **First launch** uses the Party Board preset directly, with no Classic / Party Board prompt.
- **Netcode on mobile**: the Rust netcode (`net/`, `pbnet`) builds for Android (Gradle APKs) and cross-builds for
  iOS. tvOS links a stub (no online play).

Design and hook points: [plan/NET.md](plan/NET.md). Findings per task: [research/journal/](research/journal/).

### Not done yet

This is work in progress; see [plan/TODO.md](plan/TODO.md) for the full list. Notably:

- Two real peers playing a whole match through the lobby has not been checked end to end with a disc yet
  (lobby start, drops/rejoin, match end on two processes). Single-process runs of match start, save and
  continue against the disc are deterministic.
- A scripted party crashes before reaching its end (board, turn 2) and a match start from the title screen
  segfaults; both are open.
- Desync *resync* (host sends its current state, everyone continues from the same frame, as opentennis does)
  is not done; today a desync reloads the last checkpoint.
- Autosave after every turn (not only after minigames) and autosave of offline parties.
- Per-controller button glyphs (Xbox/PlayStation/Switch instead of GameCube).
- The Online menu has not been checked by touch on a phone, and phone keyboards may autocorrect join codes.
- The iOS app has not been built (no Xcode here); the netplay hooks on macOS/MSVC builds are unchecked.
  This fork's CI has never run.

# Overview

[Build Status]: https://github.com/mariopartyrd/partyboard/actions/workflows/build.yml/badge.svg
[actions]: https://github.com/mariopartyrd/partyboard/actions/workflows/build.yml
[Discord Badge]: https://img.shields.io/discord/994839212618690590?color=%237289DA&logo=discord&logoColor=%23FFFFFF
[discord]: https://discord.gg/T4faGveujK

A work-in-progress Windows/Linux/macOS/Android/iOS port of Mario Party 4.

This repository does **not** contain any game assets or assembly whatsoever. An existing copy of the game is required.

Version Completion:

- `GMPE01_00`: Rev 0 (USA)
- `GMPE01_01`: Rev 1 (USA)

### 1. Download [Party Board](https://github.com/mariopartyrd/partyboard/releases)

Those are upstream's builds, without online play. This fork publishes no releases yet: build it from source (below).

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
