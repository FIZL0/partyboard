# N108: README refresh

## What I did

README.md's "What this fork adds" and "Not done yet" (N48's sections) rewritten from plan/DONE.md, plan/TODO.md and
the journals. Upstream's sections (Overview, Download, Building, Common problems, Credits) are unchanged; no Discord
link added (none was there).

Each new claim, and where it comes from:

- Launcher's Online button: N88 (src/port/ui/prelaunch.cpp).
- Browser build: N87 (tools/web.sh, WebGPU via emdawnwebgpu, disc kept in OPFS), N90 (6.6 MB brotli download), N93
  (Chromium 154, Firefox 157, WebKit 27.2, all Playwright on Linux).
- Browser online play over WebRTC: N92, N98 (join links, redial, 3-4 browsers). Desktop + browser: N96
  (Host > Host for Browsers).
- Autosave every turn, online and offline (local/party.json): N47. Resync to the turn start / minigame start: N46, N52.
- Gameplay-only desync check: N64 (GWSystem, GWPlayer, GWPlayerCfg, pads; only in board/minigame overlays).
- Touch pad: N70 (Settings > Input > Touch Controls). Button icons: N49 (families), N58 (touch), N59 (RmlUi menus),
  N61 (keycaps).
- Log file: N109 (not in the brief, but landed and user-visible, so listed in one line).
- Unsigned IPA on Linux: N86 (tools/ios-ipa.sh -> build/partyboard-ios.ipa).
- Platforms line: AGENTS.md (Linux, Windows, Android, iOS, browser; macOS/tvOS not targets).

"Not done yet" drops N48's items that have landed since (two-process match end N42, party crash N40, title-screen
match start N41, resync N46, per-turn autosave N47, button glyphs N49, soft-keyboard autocorrect N37, iOS app built
N86) and lists what plan/TODO.md has open: real devices (N95, N110), two physical devices, no audio (N112), no
WebGL2 path (N87/N93 journals), m411 in the browser (N105), web/desktop split on w02/w06 (N116), m444 hang
(N114/N117), w10 crash from the boot shortcut (N115), Windows define-hook route (N18), CI never ran.

Decision: the brief says "not run on a real iPhone", but plan/TODO.md N110 records the user's iPhone run (it
crashes when iOS takes focus). The README says neither an iPhone nor a simulator was used in testing and describes
the crash the user saw, rather than claiming it never ran on one.

## Not verified

- How the README renders on GitHub: not looked at there (plain GitHub Markdown, as N48's).
- Windows: the game itself has not been built for Windows by this fork either (no MSVC here); the README says only
  what the journals show (hooks built on Linux and toy MinGW DLLs, N14/N18). It still lists Windows as a target,
  per AGENTS.md.
