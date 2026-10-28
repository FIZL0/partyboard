# N133: Quit to Main Menu goes to the Party Board (prelaunch) menu

## Decision: everywhere but the browser

The menu bar's "Quit to Main Menu" now opens the prelaunch menu (Play / Online / Settings / Quit) on Linux, Windows,
Android and iOS, not only on mobile. One path is easier to keep working, and on the desktop the prelaunch menu has its
own Quit. The browser keeps 70176265's behavior (back to the game's modeseldll): it never shows a prelaunch menu
(N87: no file dialog or threads), so the prelaunch menu there would be a screen the browser build never uses.

## How: a warm reboot, not a teardown

With a valid disc, the game already boots *under* the prelaunch menu at launch: `Prelaunch::update` sets
`PartyBoard_IsGameLaunched` on its first update, `launchUILoop` ends, `game_main` runs with the menu on top (input
blocked), and Play only hides the menu (seen in a plain run: "Boot ObjectSetup" with nothing pressed). So "back to the
prelaunch menu, as at launch" = push a new `Prelaunch` and boot the game again from bootDll under it.

There is no in-process re-init of the system (`HuSysInit`: OSInit, GXInit, arena heaps, msm, ARAM, card). Running that
twice is a lot of untested surface on five platforms, and iOS can't re-exec. Instead the reboot uses the game's own
overlay switching, which already frees an overlay's processes, heaps (`HuMemDirectFreeNum`), data dirs, 3D/sprites/
windows and DLL on every switch:

1. `netplay_match_quit_to_launcher` (src/port/netplay_match.c): a running match (board, minigame, instDll, results,
   any non-menu overlay) is left by N34's `netplay_match_leave` path to modeseldll (the board through its own exit,
   BoardKill/flag (1,27)). Online, the menu bar leaves the lobby first and the lockstep's `match_left` → `netplay_end`
   → `netplay_match_leave` does it; the reboot waits while `netplay_lockstep()`. A `--match` debug party gets
   `netplay_match_end` first, as `match_left` does in a scripted run.
2. `reboot_poll` (from `netplay_match_leave_poll`, once per frame): no leave and no switch under way → `MatchRebootProc`
   kills the overlay's non-objman processes (as `MatchLeaveProc` does), lifts pauses, resets the wipe, stops sound,
   sets the overlay history to `[bootDll]` and `omOvlGotoEx(DLL_bootdll, 1, 0, 0)`.
3. bootDll runs with `SystemInitF` set: its warm path (logos skippable, the title, then modeseldll). Windows, characters
   and sound group 0 set up by the first boot are kept, as they are on a return to the title.

Prelaunch(relaunch=true) skips `HuPadInit` (it waits for retraces; the pads are set up) and, at Play, `HuCardInit`
unless the card file type changed in its Settings (each call allocates another CARD work area).

Prelaunch's own Quit: hidden on iOS (an app doesn't quit itself). On Android it stays: main returns and SDL finishes the
activity.

## Test hook

`PB_RELAUNCH_TEST="quit@89+1500,play@+200,quit@mg+300,play@+100"` (Android: `adb shell setprop
debug.partyboard.relaunch ...`): steps in turn, each once its overlay (number, `mg` = any minigame, empty = any) has run
that many UI updates in a row; `quit` is the menu bar's Quit, `play` the prelaunch Play button
(src/port/ui/menu_bar.cpp `relaunch_test_poll`). harness/relaunch_script.py turns party.script into one that taps
A+START after the quit (party.script's A taps alone never leave the rebooted title).

## Checked

- Desktop, `--script <board.script> --match 0@1500 --turbo`, `quit@89+1500,play@+200,quit@74+300,play@+100`: w01 →
  quit → board exits → modeseldll → reboot → bootDll; Play; modeseldll; quit from modeseldll → bootDll; Play → modeseldll
  → mentDll → **w01 again** (a new party from the twice-rebooted game). No errors beyond the fresh card's.
- Desktop, minigame: `quit@mg+300,play@+200,quit@1+600,play@+100` with START from 16000: quit in m4xx (ovl 23) →
  modeseldll → bootDll; Play; quit from the title → bootDll; Play → modeseldll → mentDll → w01.
- Leaks: HEAP_SYSTEM used at every overlay start after the first party is 0x19420 in both runs, through every reboot
  (the first boot's 0xa180 → 0x19420 is the first party's persistent setup, not per-reboot).
- Desktop, unscripted (real prelaunch at launch): `play@+30,quit@1+150,play@+60,quit@1+150,play@+60`: each play found
  the Prelaunch document on top (press_play succeeded), each quit rebooted.
- Android x86_64 APK on the emulator (hst34, Android 14): the same 2 cycles from the title via the property; a
  screencap shows the relaunched prelaunch menu (Play/Online/Settings/Quit) over the rebooted title; a real `input tap`
  on PLAY hides it and the game shows bootDll's Hudson logo with the touch pad.
- tools/check.sh (match_test.c got a `netplay_lockstep` stub).

## Not verified

- **Online quit (lobby leave → match_left → reboot)**: needs a two-peer lobby run (N89 harness); not run. The new code
  only waits for the lockstep to stop and then for N34's leave, which is unchanged.
- **iOS**: no build/run here (no Apple toolchain on this machine for a device run; the IPA script was not run). The Quit
  hiding is `#if !(defined(__APPLE__) && TARGET_OS_IOS)` with TargetConditionals included.
- **Quit from a board / minigame on Android**: Android runs can't be scripted to reach a board; only title quits ran.
  The reboot code is the same C as on the desktop.
- **Windows build**: not built (MinGW not run); nothing platform-specific was added beyond the Android property.
- **Prelaunch Quit on Android finishing the activity**: not tapped.
- **Play pressed before the reboot finishes** (a board's exit can take a few hundred frames): the game would show the
  board's exit / modeseldll before booting; not tried.
