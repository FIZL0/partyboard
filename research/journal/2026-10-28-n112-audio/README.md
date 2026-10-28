# N112: audio (upstream's MusyX branch merged, netplay-safe)

## Outcome

- **Merged `upstream/audio`** (zestydevy: 5fac51aa "audio support", 189ea750 "update MusyX to latest", 3ceaebfd "fix
  linux build with -fPIC") as a real merge (272e63df), upstream's commits kept. Conflicts: `files.cmake` (our port
  files + upstream's `audio.cpp`/`msm.c`) and `src/port/audio.c` (upstream deleted the stubbed copy of
  `src/game/audio.c`; the real `src/game/audio.c` is now compiled). N85's `HuAudNetplayAuxReset` moved into
  `src/game/audio.c` (under `TARGET_PC`), and `sndGrpTable` kept `SHARED_SYM`. `extern/musyx` is at upstream's
  dc56c921 (AxioDL/musyx: the PC renderer + SDL3 adapter). It built on Linux with no further changes.
- **Linux:** music and sound effects play. The title screen's stream and a whole scripted party (below) render
  non-silent PCM.
- **Netplay determinism** (`src/port/msm.c`): during `netplay_active()` (an online match and every `--script` run)
  the values the game reads back from msm come from the calls, never from the audio thread:
  - Status reads give the pre-N112 stub answers: `msmSeGetStatus` (so `HuAudFXStatusGet`), `msmMusGetStatus` and
    `msmStreamGetStatus` read 0 (N85's "done"), and `msmMusGetMidiCtrl` (so `HuAudSeqMidiCtrlGet`) reads 12 (the old
    port stub's value).
  - **Handles:** `msmSePlay`/`msmStreamPlay` return the next number of a per-session counter for every play,
    whether or not msm found a free player or voice. That depends on audio-thread timing: a full player table
    gives `MSM_ERR_CHANLIMIT`, and the counter would skip a number. Each number maps to msm's real handle (a ring
    of 256 SE / 64 stream entries), so `msmSeStop`, `msmSeSetParam` and `msmStreamStop` still reach the sound that
    is playing. `msmSeGetEntryID` (CharFXStop) maps real handles back. `msmMusPlay` returns the channel it was asked
    for (0 by default), as before. Stop/pause/param calls return 0.
  - `netplay_begin` resets the counters (`partyboard_audio_net_reset`), so every peer starts a match from 0.
  - Offline (no `--script`): msm's real answers, as on GC.
  - Debug: `PB_AUDIO_REAL_STATUS=1` gives a `--script` run the real answers, which plays offline's path from a script.
- **MusyX's clock without a device** (`src/port/audio.cpp` `keep_clock`, called once per game frame from
  `msmSysRegularProc`): sounds only end, fade or advance when something renders. With no device, a paused one, or a
  browser before its first gesture, offline waits like `while (msmStreamGetStatus(x) != 0)` (star.c, boo_house.c,
  item.c) would never end. If the output hasn't moved for 250 ms (or there's no device), the game thread renders
  the missing time into nothing, at most 1 s at once. This replaces upstream's silent-render `std::thread`, which the
  browser can't start.
- **Browser** (no threads): upstream's `sndPCStartAudio` (an `SDL_Thread`) and stream-read `std::thread` can't run.
  Under `__EMSCRIPTEN__` the port opens its own SDL audio stream whose callback renders what SDL asks for (from the
  page's audio callback), and the stream reads are serviced on game frames and in that callback. Autoplay: the
  AudioContext starts suspended. SDL resumes it once the page has had a gesture (`navigator.userActivation`). A
  capture-phase `pointerdown`/`keydown`/`touchend` listener also resumes it after any later suspend or interruption
  (e.g. iOS Safari after a call). Modifier keys and gamepads don't count as a gesture in browsers.
- **iOS:** `SDL_HINT_AUDIO_CATEGORY = AVAudioSessionCategorySoloAmbient`. SDL's default with no hint is Playback,
  which plays through the silent switch. **Why soloAmbient over ambient:** both are muted by the silent switch, as
  games are. soloAmbient stops other apps' audio rather than mixing the game's soundtrack with the user's music or
  podcast; the game's events are music-driven (w04's coin event is synced to the song). It is also iOS's default
  category for apps.
- **Background (Android/iOS):** an `SDL_AddEventWatch` pauses MusyX's device on `SDL_EVENT_WILL_ENTER_BACKGROUND` and
  resumes it on `DID_ENTER_FOREGROUND` (SDL on Android also pauses AAudio itself while it blocks on pause). N110's
  lifecycle code landed on main after this branch's base, so this doesn't call into it. It only uses SDL's events,
  which N110's hold loop keeps pumping.
- **Volume:** the game's own option (mono/stereo/surround, `option/sound.c` -> `msmSysSetOutputMode`) now reaches
  msm. Upstream applies `audio.masterVolume` from config.json (default 80) at boot. Upstream adds no Settings rows,
  so none were added; the commented-out rows are follow-up N120.
- **Gate:** `tools/check.sh` updates `extern/musyx` when it isn't at the recorded commit (a merge worktree would
  otherwise build the old MusyX). `tick_gate_test.c` stubs `partyboard_audio_net_reset`.

## Checks (GMPE01 Rev 1, `$PB_DISC`, own user dirs under build/n112; harness/ has the scripts)

- **Title screen, offline** (`harness/run_title.sh`, SDL's disk driver writing the PCM to a file, 40 s): silence
  through the boot logos, then the title stream from ~13 s to ~30 s, RMS 2500-3400, peak ~15000 (`harness/rms.py`),
  then it fades out. Same before and after the port changes.
- **Whole party, netplay mode, audio on** (`harness/party2.sh`: `tools/netplay/party.script --match 0@1500 --turbo
  --hash-log`, run1 SDL disk driver recording, run2 SDL dummy driver = other audio timing, in parallel):
  **identical for all 135000 frames**, both `match over at frame 110988`. run1's recording is 319 s of PCM, RMS
  4000-7400 in every 20 s window (music and effects throughout the party).
- **Same party on the pre-N112 build** (merge base 37919cf5 built in build/n112/base-src, `harness/base_party.sh`):
  **identical to the new build for all 135000 frames** (`hashdiff.py base2 p1/run1`), the
  same `match over at frame 110988`. So in netplay mode the game plays exactly as before N112: the merge (real
  `src/game/audio.c`, group loads, `SNDGRP_WAIT`) and the netplay answers change no frame.
- **Offline's real answers over a whole party** (`harness/party_real.sh`, `PB_AUDIO_REAL_STATUS=1`; run1 disk
  driver, run2 no audio device at all, so only `keep_clock` moves MusyX): the two runs split at frame 57593 (expected: real statuses
  follow audio time). Then their parties took different minigames. **run2 (no device) reached `match over` at
  98908**, so with no device `keep_clock` alone moves MusyX, and every sound wait the party met ended. run1 (disk
  driver) played 11 minigames, then sat in **m444** (`M444 ObjectSetup`, ovl 52) for the last 42265 frames. That's
  the known m444 party hang (N107/N108: "m444 hangs on desktop and web alike"), which this path's minigame draw
  happened to hit. Not checked further (no gdb): it hung the same way before audio existed, so most likely not
  an audio wait. (A later task on main found m444 needs the script line `plunger-stick on`; party.script on this
  branch has no such line.)
- **Browser** (`tools/web.sh` with WEB_FAST, `web_check.mjs` headless Chromium on the GPU,
  `harness/audio_probe.js` preloaded: wraps the ScriptProcessor callback, logs the AudioContext state, callbacks
  and output peak every 2 s):
  - No gesture: `state=suspended`, 0 callbacks for the whole 50 s. The game still runs: SDL's silence timer drains
    the stream.
  - `WEB_KEYS=20:Enter`: `running` from 22 s, ~47 callbacks/2 s, peaks 0.2-0.55 (title music, menu sound).
  - `20:Shift` didn't resume it: modifier keys aren't user activation.
  - Probe suspends the context at 30 s, Enter at 40 s: suspended 31-43 s, `running` again from 44 s with music
    (the port's gesture listener; SDL's own check only runs before the first resume).
- **IPA:** `tools/ios-ipa.sh` builds and `tools/ios-ipa-check.py` passes (94 Mach-O files, every import resolved).
  libdol has the soloAmbient hint string and `sndPCRender`.
- **APK:** `./gradlew :app:assembleDebug` (arm64, and arm64+x86_64) builds. On the x86_64 emulator (hst34) the game
  logs `Audio output: 44100 Hz, 2 channels` and `dumpsys audio` lists the game's `type:AAudio ... state:started`
  player (USAGE_MEDIA).
- `tools/check.sh` passes.

## Tried and failed

- First party runs: relative output path while the script `cd`s into the game dir; no logs. Fixed with an absolute
  path.
- First baseline and real-status parties ran during the iOS/Android/web builds and the emulator (load ~33): all
  three were still on the board when their 1500 s ran out (45041, 39788, 24354 frames). Not hangs: all were in
  ovl 89 at different frames, and the two real-status logs were identical up to the shorter one. Rerun at lower load
  (above). The partial baseline matched the new build's log for all 24354 frames.
- Android background check: on this branch's base, HOME left the game thread stuck (pre-N110: `surfaceDestroyed`,
  `nativePause`, then `Activity stop timeout`), so the audio pause never ran. On a throwaway local merge with main
  (N110; detached HEAD, not committed), the x86_64 APK's UI thread got stuck too ("top resumed state loss timeout",
  no SDL `onPause` log, black game view, adb restarted under me once). I stopped there: follow-up N119.

## Not verified

- **A real two-peer online match with audio on.** The determinism checks are two `--script --match` runs of the same
  pads with different audio drivers (netplay_active, the same code path a match takes); no lobby/lockstep pair
  (N89/N38 harness) was run. The answers that reach the game in a match come from `src/port/msm.c`'s netplay
  branch, the same one exercised here.
- **Desktop + browser in one match with audio on:** not run.
- **Android/iOS background pause at runtime:** see above (N119). iOS: no device here; the IPA is only checked
  statically. The soloAmbient category and the silent switch need a real iPhone.
- **Audio on the Android emulator being non-silent:** only the AAudio player's `started` state was seen, not its
  samples. Linux and the browser checked the PCM.
- **Real devices** (latency, crackle, CPU cost of MusyX on phones): none here.
- **Offline play by hand** (no script): only the title screen ran unscripted. Offline's path over a party was
  played from a script with `PB_AUDIO_REAL_STATUS=1`.
- **w04's music-synced coin event in netplay** keeps reading the old 12 for the MIDI controller and may not pass its
  20-29 wait (pre-existing: the stub was 12 before N112). Board music doesn't pause for events during netplay
  (`BoardAudSeqPause` checks status 3). Follow-up N118: a frame-locked MusyX during netplay would give real
  statuses on every peer.
- **Windows build:** not built (the task names Linux, Android, iOS, browser).
