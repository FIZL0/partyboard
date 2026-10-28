# N124: Settings > Audio, live Master Volume

## What changed

- `src/port/ui/settings.cpp`: the commented-out Audio tab is now a real tab (between Input and Gameplay) with one
  row, **Master Volume** (0-100 %, step 5, `audio.masterVolume`, default 80). Each change saves config.json and
  calls `partyboard_audio_set_master_volume`.
- `src/port/audio.cpp` / `include/port/audio.h`: `partyboard_audio_set_master_volume(s32)` (clamps 0-100).
  `sMasterVolume` is now a `std::atomic<s32>`: the output callback reads it on SDL's audio thread, the UI writes it
  on the game thread. Boot (`partyboard_audio_pre_init`) goes through the same setter.

The gain is applied to the mixed s16 output after MusyX renders (the existing N112 code path), so no game, MusyX
or msm state sees it: hash logs and netplay are unaffected, and both peers may use different volumes.

## Decision: the other audio.* keys stay unmapped

`audio.mainMusicVolume`, `subMusicVolume`, `soundEffectsVolume`, `fanfareVolume` could map to
`msmMusSetMasterVolume` / `msmSeSetMasterVolume` / `msmStreamSetMasterVolume`, but the game owns those calls:
`src/game/sreset.c` sets all three to 0 on soft reset, and `src/REL/selmenuDll/main.c` toggles the music master
between 0 and 127. A user value written there would be overwritten (or would undo the game's mute). Doing it
properly needs a scale factor inside msm (user % times the game's value) — not done, no follow-up filed since no
one asked for per-bus volume. `enableReverb` / `enableHrtf` have no backend in this port; `menuSounds` has no
menu sounds to gate. All of these stay registered config keys without a UI row (comment in the tab says why).
`portmain.cpp`'s old commented `SetMasterVolume` TODO was left alone (not my code; boot already applies it in
`partyboard_audio_pre_init`).

## Checks

- Build (linux-default-relwithdebinfo) clean.
- **Live gain on real audio** (`harness/live_volume.sh` + `live_volume.gdb`): offline boot to the title screen,
  SDL disk driver to a raw file, config at 100, then `partyboard_audio_set_master_volume` called from gdb at game
  frames 900/1080/1260 (50, 0, 100). Per-second RMS/peak (N112 rms.py):
  `14s 4132/18951 -> 16-18s ~1850/~9400 (50%) -> 20-21s 0/0 -> 22-28s ~3900/~18000`.
  First attempt put the steps at frames 1200/1800/2400 and looked like "back to 100 stays silent": a control run
  (`control.gdb`, all calls at 100) showed the title music fades out on its own at ~30 s, so the steps were moved
  inside 13-29 s.
- **The UI row** (`harness/ui_hook.patch`, a temporary hook in `ui::update`, reverted): opens SettingsWindow,
  activates tab 2 (Audio), dispatches Left, Left, Right keydowns on the row. Log: row text
  `Master Volume • 100%` -> 95% -> 90% -> 95%, setting follows, config.json ends with `"audio.masterVolume": 95`.

## Not verified

- The row looked at on screen (focus highlight, help text in the right pane, touch/mouse drag): only its RML and
  values were read from the hook; the hidden window can't be screenshotted with RmlUi (aurora_capture_frame grabs
  the EFB before RmlUi).
- That the UI's setter reaches the audio thread in the same run: the hook run used the dummy driver; the audio
  path was checked separately by calling the same setter from gdb.
- Browser, Android, iOS, Windows at run time: same C++ path (the web build uses the same output_callback), not run.
