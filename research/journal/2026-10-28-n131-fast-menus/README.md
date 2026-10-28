# N131: Fast Menus

User (2026-10-28): "improve the main menus by removing the long animations or speeding them up. it takes forever".

## Where the time went (before)

GMPE01 Rev 1, fresh user dir (new memory card file), all four ports tapping A for one frame every 6 frames
(`harness/tap.script`; START on port 0 in bootDll), real time (vsync off, 60 Hz frame limiter), launch to the board
(w01). `harness/timed_run.sh` with the scratch trace (`harness/trace.diff`: overlay starts, wipes, messages with
`GlobalCounter`; plus a PNG every N frames through `aurora_capture_frame`).

| Screen | frames | before (s) |
| --- | --- | --- |
| bootDll: Nintendo logo (wipe 30 + 3 s + wipe 30), 60-frame gap, Hudson logo (same), 1 s, THP slot (no movie on PC: wipe 10 + 30 + 60-frame sleep), title (wipe 30, 50-frame logo zoom; START only) | 1 → 832 | 13.85 |
| modeseldll: slot select, file select / new file, envelope opening (~200 frames), card fan, mode pick, card fly-off (~180 frames) | 832 → 1569 | 12.29 |
| mentDll (party setup): bubble intro + camera zoom (~400 frames before the first text), player count, character select, board pick (panel slides ~60 frames each), curtain closes, Toad walks in, settings, curtain opens, present, flash | 1569 → 3648 | 34.65 |
| **launch → board** | | **60.8** |

The launcher (prelaunch.cpp/rcss): body fades in over 1.2 s (0.2 s delay + 1 s), the buttons are staggered
0.2–0.7 s delays + 0.3 s each, and it fades out over 1.2 s on Play (the game boots under the fade).

Message speed was not a cost: the menus set window speed 0 (instant) or 1 (3 characters a frame), and every
message waits for A.

## What changed

One setting: Settings > Gameplay > General > **Fast Menus** (`game.fastMenus`, on by default). The task said
"Settings > General"; there is no General tab, so it is the (so far commented-out) General section of the Gameplay tab.

- `src/port/fast_menus.c`, `include/port/fast_menus.h`: in bootDll, modeseldll, mentDll, mpexDll and subchrselDll the
  game runs 2 frames per shown frame: the others are not presented (`aurora_set_present_skip`, N120's turbo
  mechanism) and `src/game/main.c` skips their `frame_limiter`. Holding **A or START** on any pad makes it 4 (the menus
  act on presses, not holds, so a held A only fast-forwards what its press started). Holding a direction (D-pad or
  stick, `HuPadDStk`) drops to 1, so cursor repeats keep their pace. The game computes exactly the same frames as
  without it; only fewer of them are shown. So all animations, camera moves, fades and text are covered at once,
  instead of patching dozens of decomp state machines in mentDll one by one.
- `src/REL/bootDll/main.c`: on the first boot the Nintendo/Hudson logos and their fixed 3 s waits are skipped
  (`BootInitForFastMenus` runs their setup: CharInit, windows, title models, sound group 0). The title takes A as
  well as START, and takes either during the logo's zoom. The attract loop (title idle 30 s → logos) is unchanged.
- `src/port/ui/prelaunch.cpp` + `res/rml/prelaunch.rcss`: `body.fast` gives 0.25 s fades and a 0.15 s entrance
  without the stagger.
- `src/port/netplay.c`: script line `fast-menus on`. **A `--script` run gets Fast Menus only with that line**, so
  every script tuned to the menus' frames (mash-a.script, party.script, save-continue.py, ...) keeps them.

Never on during netplay: off while `netplay_active()` (real matches start on the board; after one ends it is on
again in the menus); in a script run off once its `--match` runs or under lockstep. Boards and minigames are never
in the list of overlays, and nothing hashed is touched (the present skip only drops GPU work).

Not cut: the "Creating file" memory-card wait and the card/menu logic itself; they just run at 2x/4x.

## After

Same pads, same measurement (`harness/tap-fast.script` = tap.script + `fast-menus on`; `harness/hold-fast.script`
holds A 5 of every 6 frames). Game frames per screen are unchanged except bootDll.

| Screen | before (s) | after, tapping (s) | after, holding A (s) |
| --- | --- | --- | --- |
| bootDll (logos + title) | 13.85 | 0.80 | 0.70 |
| modeseldll (file + mode select) | 12.29 | 6.15 | 4.13 |
| mentDll (party setup) | 34.65 | 17.34 | 11.55 |
| **launch → board** | **60.8** | **24.3** | **16.4** |
| launcher fade in / fade out | 1.2 / 1.2 | 0.25 / 0.25 | |

(The tables count from the game's first frame; the launcher is skipped in script runs.)

A shot sheet of a fast run (PNG every 15 frames) looked right: title at once, file select, envelope, mentDll's
bubbles and panels drawn normally; no stale or black frames from the unshown ones.

## Hash check

`harness/party_pair.sh` (writes the `fast-menus on` copy of the script itself): tools/netplay/party.script (`--match 0@1500 --turbo`, whole party to `match over`) as is
and with `fast-menus on`, then `hashdiff.py`. With it the boot is ~740 frames shorter, so at frame 1500 the match
starts from mentDll's intro instead of modeseldll; the hash log from the match start must not care.

Result: both reach `netplay: match over at frame 96948`, both end at frame 135000 with hash `B6170562862FD181`.
The whole logs differ from frame 153 on: that is the menus before the match (the fast run's boot skips the logos),
which is the point. From the match start (frame 1500) on, `hashdiff.py` on the two logs cut at 1500 says
`identical: 133500 frames`. Its only note is the heap layout (`heap=`, not state) differing from frame 1500: the
match was set up from another menu overlay (mentDll vs modeseldll, "killed 13" vs "killed 2 menu processes"), as
with two peers waiting in different menus.

Under `--turbo` main.c doesn't run the present skip (turbo has its own), so this run covers the bootDll changes and
the script switch, not the 2x/4x pacing. The pacing changes no game state: the same game frames run, the skipped
ones are not drawn.

## Tried and dropped

- The first guard was `!netplay_active()` alone: a `--script` run has a netplay session from frame 1 (for its hash
  log), so `fast-menus on` did nothing. Scripts now check `netplay_match_running()`/`netplay_lockstep()` instead.
- The first tap script held A 3 of 6 frames, which already triggers the 4x hold speed; the numbers above use 1-frame
  taps for "tapping".

## Not verified

- Real-device feel on Android/iOS/browser (not run: only the desktop build was run). The mechanism is the same
  aurora present skip that `--turbo` uses on every backend, but a browser's requestAnimationFrame pacing with
  skipped presents was not tried. The 4x hold and 1x direction rules are untested by a human.
- Timing with vsync on in a visible window: all runs here had vsync off (hidden windows with vsync crawl at ~10 fps).
  A skipped frame does not present, so it should not wait for vsync, but this was not measured on a shown window.
- PAL/JP discs: only GMPE01 (US) exists here. The title's A/zoom skip is on the NTSC branch only; the logo skip runs
  after PAL's language menu and was not run on PAL.
- mpexDll and subchrselDll (Extra mode, minigame mode char select) are in the overlay list but were not driven.
- Audio at 2x/4x: sound effects play at the game's rate, music is not sped up; not listened to (hidden window).
- A real two-peer online run (lobby, two copies) was not done: the hash check is the two-run party compare above.
  Fast Menus is off while `netplay_active()`, so a lobby match itself never sees it.
