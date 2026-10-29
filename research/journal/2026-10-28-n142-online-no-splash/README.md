# N142: Online from the home screen goes straight to the lobby, without the splash

## What changed

- Launcher > Online no longer boots the game as Play does (N88: Nintendo/Hudson logos, then the title and its music
  under the Online window). `Prelaunch::launch_online` calls `quick_online_request()`. bootDll's first boot
  (`omovlevtno == 0`) then takes `QuickOnlineTake()`, after the N132 Continue check. It runs
  `BootInitForSkippedSequence` (the skipBootSequence/Continue setup: characters, windows, sound group 0, `SystemInitF`)
  and `BootLobbyExec`. That shows a quiet navy backdrop (`Hu3DBGColorSet(16, 24, 48)` and a wipe-in) and plays
  **seq 43**, the music that filesel.c / modeseldll play on the card/file select (the music the user asked for in
  N141). It waits while `QuickOnlineHeld()` is true: an `OnlineWindow` or `LobbyWindow` is open
  (`ui_is_online_open`), a pbnet lobby exists, or a match runs. There is a 30-frame grace period, so a frame between
  two windows does not end the wait.
- The match start is unchanged. `MatchStartProc` kills BootExec as a menu process ("killed 1 menu processes", the
  same as from the title) and fades every seq out.
- **Boot hold.** With a verified disc the game boots *behind* the launcher, so by the time Online is pressed bootDll
  would already be past its first-boot check. N132 held the boot only while Continue was offered. Now the launcher
  also offers Online (`quick_online_offer(TRUE)` whenever a disc is loaded, or when Online is pressed without a disc,
  before the disc picker), and `QuickContinueHold` waits for either offer while the launcher is open. Play after the
  hold boots normally (logos, then title), as N132's Play after a hold did.
- **Leaving without a match** (the Online window closed, no lobby): the music fades, the backdrop wipes out, and
  BootExec goes to the title through the same branch it uses when coming back from the mode select
  (`omovlevtno != 0 || lobby_left`: Hudson sprite, `BootTitleCreate`). No logos. `HuWinInit` is a no-op the second
  time (`winProc` guard). Log line: "boot: left the Online menu without a match, on to the title".
- Files: `src/REL/bootDll/main.c`, `src/port/quick_continue.c` + `include/port/quick_continue.h` (the
  launcher→bootDll requests live with N132's), `src/port/ui/prelaunch.cpp`, `ui.cpp/ui.hpp/compat.cpp/ui.h`
  (`ui_is_online_open`). `src/port/netplay_match.c` needed no change.

Decision (unattended): the backdrop is a plain colour, not disc art. N141 (home screen remake, not merged yet) owns
the art. Under the Online window the plain colour is what the brief calls a quiet backdrop, and it costs nothing at the
match start. When N141 lands, its backdrop/music code could replace `BootLobbyExec`'s colour.

## Checks (disc GMPE01-01, `$PB_DISC`, relwithdebinfo)

Harness: `harness/` = N122's pair.sh (N89 two copies, one user dir, host + guest by code, both through the
**launcher's Online**) plus `n142_test.inc` (shotui = `aurora_capture_frame_ui`, overlay change log). Hook it with
`harness/apply.py` (`--revert` takes it out). `pair.sh <name>`, `SOLO=1 HOST_SCRIPT=play.txt|leave.txt pair.sh <name>`.
Output goes to `build/n142/<name>/`. Screenshots are not committed (disc art).

| run | result |
| --- | --- |
| a (pair) | Both copies: overlay 1 (bootDll) from frame 3 until the match start, no other overlay. EFB at host frames 200/500 and guest 400: plain navy, no logos or title. UI shots: the Online window / lobby on the navy. Match: "killed 1 menu processes" on both, board 0 at host frame 1500. **Hash logs, session 1 (the match): identical for all 4011 common frames** (hashdiff; the guest's log is 2 frames longer because it quit later). Host audio (SDL disk driver): silent for 2 s, then RMS 1100-3000 (music) through the lobby and into the board |
| play (solo) | Launcher > Play: the Nintendo logo at frame 230. Play is unchanged |
| leave (solo) | Launcher > Online, then Escape at frame 400: "boot: left the Online menu without a match, on to the title". The title screen (PRESS START) is up at frame 1100. The menu bar is open (N88's close behaviour) |

`tools/check.sh`: run by `agent-runner done`.

## Not verified

- **Desktop + browser (N96) pair:** not run. It needs the web build rebuilt (tools/web.sh) and N96's harness taught to
  go through the launcher (its desktop uses `skipPreLaunchUI`, so that path never hits this change). The web build
  takes the same prelaunch.cpp/bootDll code. Only the desktop pair's hash logs were compared.
- **That the lobby music is seq 43 by ear:** only the capture's RMS shows music playing. The overlay never left
  bootDll and the title was never created, so the title's stream did not play, but nobody listened to the audio.
- **A mixed pair** (one peer via Play + menu bar > Online, sitting on the title; the other via launcher Online): not
  run. Both peers already came from different menu states before this change (the title loop at different points).
- Android/iOS: not run on a device or emulator. The code path is shared C/C++.
- The no-disc path (Online → disc picker → verification → Online) was not run in-game: it needs N88's fake-pick hook.
  The new `quick_online_offer` there only makes the boot wait for the press.
- Continue while Online is also offered: Continue's own hold path is unchanged, but it was not rerun.
