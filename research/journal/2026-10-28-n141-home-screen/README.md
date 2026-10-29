# N141: the Party Board home screen, remade

User (2026-10-28): "the homescreen sucks, i cant press the settings with touchscreen, lets remake it". Later in
the task: check it at iPhone 15 Pro size (landscape and portrait, with Dynamic Island and home-indicator insets) and on
a small phone (1334x750), with screenshots.

## What changed

- **Layout** (`res/rml/prelaunch.rcss`, document in `src/port/ui/prelaunch.cpp`): one flex layout, with no
  absolutely placed text over the buttons. Desktop and tablets: the eyebrow and logo over a column of full-width
  buttons on the left, and two status pills at the bottom (disc on the left, version and crash note on the right).
  Landscape phones (`max-height: 640dp`): the logo on the left, the buttons on the right (they scroll if three
  Continue entries make the list too long). Portrait: one centred column. Buttons are at least 64 dp tall on
  desktop, 57 dp on landscape phones and 65 dp in portrait (measured below). They have dark translucent fills, and
  hover or focus turns them blue (#009dda), so they read over any backdrop.
- **Safe area**: the layout box's edges are set from `safe_area_insets()` (N130) each update
  (`Prelaunch::apply_safe_area`). With the default settings the RmlUi context is already the window's safe rect
  (aurora fits the picture inside the present insets), so nothing can reach the notch strips. This keeps it right
  with Lock 4:3 too.
- **Backdrop from the disc** (`src/port/ui/home_art.cpp`): `pbdisc://title-bg` is an aurora runtime texture. It holds
  the title screen's background (`data/title.bin` file 8 on the US disc, a 640x480 RGBA8 starburst;
  `TITLE_ANM_VER_ADJUSTED(TITLE_BG_ANM)`). It is read through the DVD API once the game has opened the disc, decoded
  with the game's own `HuDecodeData` into our own buffers, then the ANM bitmap is untiled (RGBA8/RGB5A3). Nothing is
  written to disk or committed. If file 8 is not that kind of picture, the archive is scanned for the first one that
  is (region-proof). It moves gently: `home-burst` (scale 1.05 to 1.25 with ±3° over 40 s, alternating) plus a
  breathing glow. Without a disc (or before one is picked) the menu's own `prelaunch-bg.png` drifts instead (30 s).
- **Music** (`src/port/home_screen.c`, called from `QuickContinueHold`): with a disc, the game boots *behind* the
  menu (Prelaunch::update sets PartyBoard_IsGameLaunched). Before N141, bootDll went on to the logos and the title
  under the menu, with their sounds and the title stream (the "splash music" the user disliked). Now the first boot
  (event 0) waits while the menu is open, as N132's Continue hold already did, but whether or not Continue is offered.
  Meanwhile msm plays song 43, the card/file-select music (`HuAudSeqPlay(43)` in modeseldll filesel.c), from the disc
  through MusyX. It fades out (500 ms) when Play, Continue or Online closes the menu, and the boot goes on as before.
  Song 43 needs group set 0 (`MSM_GRPSET_INIT`): the hold loads it exactly as bootDll does after the logos
  (`msmSysLoadGroup(0)` + `HuAudSndGrpSetSet(0)`, as in BootInitForSkippedSequence), so the boot's own loads become
  no-ops and the boot ends with the same groups. Master Volume applies, because it is the output gain (N124). With
  no disc there is no game, no MusyX and no audio device: silent. `PB_HOME_MUSIC=0` turns the music off.

Offline and online play are unchanged otherwise. The hold returns at once when there is no menu (`--script`, the
browser, skipPreLaunchUI) and during netplay. An online match starts long after (lobby, mode select), on a MusyX
the session settles, with netplay's own msm handles (N122). The hold's extra `HuMemDirectMalloc`/`Free` pair on
HEAP_DATA only shows in `heap=`, which is not part of the state hash.

## Tests (desktop, `harness/`: `harness.diff` is a temporary UI hook, `run.sh` runs one game per user dir)

The harness resizes the window through Hyprland (`hyprctl eval "hl.dispatch(hl.dsp.window.resize({x=..,y=..,
relative=false, window='pid:N'}))"`; the desktop's floating rule ignores SDL_SetWindowSize, and aurora's 640x480
minimum has to be lowered first). The display scale here is 1.6, which is RmlUi's dp ratio, so a window of
852x393 logical px lays out exactly like an iPhone 15 Pro's 852x393 pt at 3x. `PB_SAFE` fakes the insets.
Screenshots are `aurora_capture_frame_ui` (game + RmlUi, at the context's size); they show disc art, so they are
not committed.

| case | window (pt) | PB_SAFE l,t,r,b | context (dp) | buttons (dp) | hit test |
|---|---|---|---|---|---|
| desktop | 1280x960 | none | 1280x960 | 453x70 | all ok |
| iPhone 15 Pro landscape | 852x393 | 59,0,59,21 (island, home indicator) | 734x372 | 335x57 | all ok, none clipped |
| iPhone 15 Pro portrait | 393x852 | 0,59,0,34 | 393x759 | 345x65 | all ok |
| small phone landscape (1334x750 px = 667x375 pt) | 667x375 | none | 667x375 | 301x57 | all ok |
| small phone portrait | 375x667 | 0,20,0,0 | 375x647 | 327x65 | all ok |
| no disc, desktop / iPhone landscape | 1280x960 / 852x393 | none / 59,0,59,21 | | 410x70 / 335x57 | ok |

The text is the N64 Party font at 26-32 dp on the buttons, the status pills are 15-18 dp, and the pill details 13 dp
on landscape phones (the smallest text, still readable in the shots). Nothing is under the insets.

By finger, sent in iOS's order (SDL's touch-mouse copy `SDL_TOUCH_MOUSEID` first, then the finger, as SDL_touch.c
does): Settings opens at every size. Every Settings tab works by tap in landscape (Video, Input, Audio, Gameplay,
Cheats, Interface, Advanced, Prelaunch), and tabs past the edge come into view with a swipe of the tab bar (RmlUi
touch scrolling) or by tapping the next visible tab. Rows: Fast Menus toggles once per tap, and Master Volume gets
focus. Play goes to the title screen, Online opens the Online window, and Quit exits (exit 0 in 5 s).
Pad/keyboard: down, down, enter opens Settings from Play; escape closes it back to the menu (focus back on Settings).

Audio (SDL disk driver, `research/journal/2026-10-28-n112-audio/harness/rms.py`): song 43 plays from the menu's
first second (RMS 1400-2200 every second for 37 s, no gap). After a Play tap it fades and the game's title stream
takes over. Master Volume 0 gives RMS 0 every second. Without a disc, no audio device is opened.
Relaunch (`PB_RELAUNCH_TEST="play@+200,quit@+600,play@+400"`): music, fade at Play, music again on the re-opened
menu after the menu bar's Quit, and a fade at the second Play.

`tools/check.sh` passes. `tools/ios-ipa.sh` built build/partyboard-ios.ipa (28 MB), and `tools/ios-ipa-check.py`
passes it (94 Mach-O, every import resolved). `./gradlew :app:assembleDebug` (Temurin 21) built the arm64 and
x86_64 APKs, which hold home_screen.c, home_art.cpp and the new prelaunch.rcss. Neither was run on a device or the
emulator.

## The touch bug: what I found, and what I could not reproduce

SDL turns every touch into mouse events too (SDL_TOUCH_MOUSEID, sent *before* the finger's events), and aurora's
rmlui.cpp gives RmlUi both. I suspected one tap made two clicks, so Settings would open and be pressed again. On
desktop, with those copies in the iOS order, Settings still opens once and Fast Menus toggles once: RmlUi turns the
doubled press into one click. I wrote an SDL event filter that dropped the copies, then **took it out again**,
because it fixed nothing I could show.

The second theory was the old layout: on landscape phones its disc-info box sat absolutely at the bottom right over
the button column. A tap on the 4th button's label (where Settings is on iOS once a Continue entry shows) still
reached the button (it was Quit on desktop, and the game quit). The old buttons were 54 dp tall, and at iPhone size
the column ran down into the bottom inset area with the status text over it. The new layout puts every button in
flex flow above the footer, away from the screen's bottom edge, where iOS defers touches near the home indicator.
That last part is a guess: I can't test it without a device.

## Not verified

- **On a real iPhone or Android phone** (none here): the original "can't press Settings" was never reproduced on
  desktop, so the new layout fixing it on iOS is not proven. Worth a check on the user's phone: tap Settings, tap
  a Settings row.
- Portrait on iOS: the app is landscape-only (aurora sets `SDL_HINT_ORIENTATIONS` LandscapeLeft/Right). Portrait
  was checked as a desktop window only. In portrait the **Settings window** (not the home screen) has a tab bar wider
  than the screen. Swiping scrolls it, but I did not tap through all its tabs in portrait.
- The Continue button by finger: a fresh user dir has no game in progress. It is the same Button as Play, and its
  press path (Prelaunch::continue_game) did not change.
- The disc picker by finger (Select Disc Image opens the system dialog; nothing to tap there on desktop).
- Online matches and hash logs: the hold does not run during netplay or in `--script` runs, so no hash-log run was
  done. A desktop peer through the menu against a browser peer without the menu was not run as a pair.
- A PAL disc: the art loader scans title.bin for the picture if file 8 is something else. Only the US disc is here.
- Music looping past one pass: the 37 s capture never went silent, but I did not time the song's own loop point.
