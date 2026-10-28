# N122: netplay sound answers from a frame-locked MusyX

## What changed

- **Frame clock during netplay** (`src/port/audio.cpp`): while `netplay_active()` (an online match and every
  `--script` run), MusyX's device no longer pulls the engine along. The game thread renders **10 cycles of 5 ms every
  3 game frames** (3, 3, 4: 50 ms per 3 frames at 60 fps, as the GC's DSP did per field) in `msmSysRegularProc`
  (`HuPadRead`, once per game frame), and queues the PCM to the device. msm's real statuses then depend only on the
  frames played, so every peer reads the same values on the same frame:
  - `msmSeGetStatus`, `msmMusGetStatus`, `msmStreamGetStatus`, `msmMusGetMidiCtrl` give msm's real answers again
    during netplay (board music pauses: `BoardAudSeqPause` sees status 3; w04's coin event follows its song's MIDI
    controller; jingle waits wait).
  - N112's handle counter stays: the game still sees play numbers 0, 1, 2... from the match start, and a status read
    maps the number to msm's handle (a play msm refused or one 256 SE / 64 stream plays old reads 0).
  - Stop/pause/param return values stay N112's (values of the call).
- **One output path on every platform:** the port opens its own SDL audio stream (the browser path of N112) on
  desktop/Android/iOS too, instead of MusyX's SDL adapter (`sndPCOpenAudio`/`sndPCStartAudio`, whose own thread
  renders and can't be stopped without pausing the device). Off netplay its callback renders what the device asks
  for (as before, under the audio lock); while locked it renders nothing and the game thread's PCM plays (a shortfall
  is silence).
  - Lock order: SDL takes its stream lock before calling the callback, which takes the audio lock; the game thread
    therefore never calls SDL stream functions while holding the audio lock (`locked_frame` renders, unlocks, then
    queues).
  - Queue: under 1 game frame's PCM (an underrun: a lockstep wait, a load) the game thread first adds silence up to
    3 frames (50 ms of latency); over 8 frames (turbo, a device clock slower than the game's) that frame's PCM is
    dropped. Background (mobile): nothing queued.
- **One mix rate, 48000 Hz, everywhere** (SDL converts to the device's rate), also with no device at all (MusyX's
  default would be 32000). Where a sound ends after N cycles depends on the mix rate (MusyX PC resamples per output
  frame: `synth_vsamples.c` nextSamples, the voices' polyphase positions), so two peers with 44100 and 48000 Hz
  devices would read different statuses. Channels still follow the device (2/4/6/8; the browser 2).
- **Stream reads** (`msmStreamDvdCallback` sets stream status): while locked they are done on the game thread
  before each cycle, not on the read thread (which now leaves the queue alone while locked). Entering the lock waits
  for the read the thread has under way.
- **Match start** (`partyboard_audio_net_reset`, from `netplay_begin`, every epoch of an online match: start,
  continue, rejoin, resync): `partyboard_audio_net_clock_begin` locks the clock (finishes the cycle the device was in),
  then every SE, song and stream is stopped at once, 40 cycles render into nothing (the stops' 0-speed fades), MusyX's
  `last_rnd` goes back to 1 and the 3-frame cadence restarts. So every peer starts from the same engine state
  whatever its menus played. (`--script` runs begin their session before msm is up: the fresh engine is that state.
  Their `--match` start only reseeds, as before.)
- **SNDGRP_WAIT** (`src/game/audio.c`: after stopping everything, poll `msmMusGetNumPlay`/`msmSeGetNumPlay` until 0
  or 500 ms of `OSGetTick`): on a frame clock nothing would end inside that loop, and the group would be deleted
  under playing voices. `msmSeGetNumPlay`/`msmMusGetNumPlay` got locked wrappers in `src/port/msm.c` (`msmpc.h`):
  during netplay each poll that reads sounds still playing lets one 5 ms cycle pass (as the DSP would while the GC
  polled), and after 100 in a row (500 ms) it reads 0. Both depend only on msm's state.
- Leaving netplay (`netplay_end`): the next frame gives the clock back to the device (`partyboard_audio_frame`).
- Debug: `PB_AUDIO_NET_STUBS=1` gives netplay N112's answers (0 and 12) on the same clock; `PB_AUDIO_REAL_STATUS=1`
  still gives a `--script` run offline's path (device clock, msm's own handles).

## Checks (GMPE01 Rev 1, `$PB_DISC`, own user dirs under build/n122; harness/ has the scripts)

- **Whole party, two audio setups** (`harness/party2.sh`, N112's: `tools/netplay/party.script --match 0@1500 --turbo
  --hash-log`, run1 SDL's disk driver recording to a file, run2 `DRIVER2=none`, no device at all, so no output and no
  device timing): **identical for all 135000 frames**, both `match over at frame 96948` (N112's stubs: 110988, so the
  real statuses do change the party). run1's recording is non-silent throughout (RMS 2000-5000 per second,
  `rms.py`). No `Timed Out!` from SNDGRP_WAIT in either run.
  - The first try split at frame 56471 (heap layout from 56390, ovl 89): with no device the port never called
    `sndPCConfigure`, so MusyX mixed at its default 32000 Hz against the disk run's 48000. Fixed by configuring 48000
    with no device too; that is the run above.
- **w04's coin event** (`harness/coin.sh`: mash-a, `--match 3@900 --turbo`; gdb gives player 0's first walked space
  the 0x400000 flag, makes player 0 small and answers the choice yes; logs mg_coin's steps and the song's MIDI
  controller 1 and status on change), run with SDL's dummy driver and with no device:
  - Both logs and both hash logs **identical** (9000 frames). The event plays through: song (0x1D, channel 1) starts
    at 4763, ctrl 10 at 4890 (`fn_1_D230` passes), **20 at 4920** (`fn_1_D2A4` passes: the 20-29 wait N112 noted),
    30 at 4980, then 11/21/31 ... 17/27/37 every 30 frames (7 rounds), 40 at 5910, song done (status 0) at 5940,
    "BGM1 Restore", `fn_1_E144`/`fn_1_D0DC` (event over) at 6338/6365.
  - Same harness with `PB_AUDIO_NET_STUBS=1` (N112's answers): ctrl 12 and status 0 from the start, "BGM1 Restore"
    at once, and the event sits in `fn_1_D2A4` until the run ends (frame 9000).
- **Two real peers through the lobby** (`harness/pair.sh`: N89's two copies, host + guest by code, `n89_test.inc`
  hooked into `src/port/ui/ui.cpp` for the run only, `--hash-log` on each; host on SDL's dummy driver, guest with no
  device; the guest started 3 s later, so their menus played different audio): both logged "MusyX on the game's
  frame clock" at the match start (`netplay_begin`'s reset: stops, 40 settle cycles). Hash logs split per session
  (N38's split.py): the match session **identical for 4049 frames** (board intro into the first turn's roll, where
  no one presses anything; the guest ran 2 more frames before its quit).
- **Offline title screen** (`harness/run_title.sh`, disk driver, 40 s): silence through the logos, the title
  stream from ~12 s to ~30 s (RMS ~2900-3400), as in N112: the port's own stream callback renders offline.
- Every `while (...status...)` wait in `src/` sleeps a frame per poll (grep), so none can spin inside a frame on
  the frame clock; the only in-frame one is SNDGRP_WAIT (handled above).
- **Browser vs desktop, whole party** (`WEB_FAST=1 tools/web.sh`; `tools/web_check.mjs` headless Chromium on the
  GPU, N116's way: party.script with `end 999999` so the page still answers, `--match 0@1500 --turbo --hash-log`,
  2400 s): `match over at frame 96948` as on desktop, and its hash log is **identical to desktop run1's for all
  135000 frames**. (No gesture in the headless page: its AudioContext stays suspended, so the browser's PCM goes
  into a stream nobody plays; the clock doesn't care.)
- `tools/check.sh` passes.

## Tried and failed

- First party pair split at 56471: the no-device run mixed at MusyX's default 32000 Hz (see above).
- coin.sh's first version let mash-a answer the event's choice: it read 1 ("no", `BoardMGExit` at once). gdb now
  sets `winChoice = 0` at the `BoardWinChoiceGet`.

## Not verified

- **netplay_end giving the clock back to the device** (`partyboard_audio_frame` sees netplay off): scripted runs never
  end their session, and the lobby pair was stopped mid-match. The code path is the same `clock_begin` call.
- **A resync/rejoin/continue epoch** (a second `netplay_begin` mid-match with sounds playing): only the first match
  start's reset ran (lobby pair). The reset is the same call each epoch.
- **Residual engine state across the reset:** everything is stopped and settled and `last_rnd` reset, but msm's and
  MusyX's internal tables (voice allocation order, msm's own handle numbers) still carry the menus' history. Statuses
  of the match's sounds matched in the lobby pair (4049 frames), not over a whole online party. If a two-peer split
  ever points at audio, that is the place to look (or reinitialise msm at the reset).
- **Peers with different channel counts** (a 5.1 device vs stereo): only stereo and no device ran. Channels change
  the mix, not (as far as the code shows) voice lifetimes; not tested.
- **SNDGRP_WAIT's wall-clock 500 ms** is still in the game's loop: if 100 cycles of rendering ever took longer than
  500 ms on a peer, it would stop polling earlier than the others. Each poll renders one cycle of a stopped engine
  (microseconds); no `Timed Out!` was seen.
- **Real speakers:** latency, underruns and crackle of the frame-locked queue (3 frames of silence after a dry
  queue, drop over 8) were not heard, only recorded (disk driver, which paces in real time) and checked non-silent.
  Turbo runs drop most frames' PCM by design.
- **Android/iOS:** not built or run here (same C++ path as Linux, `PB_AUDIO_THREADS`; desktop's MusyX SDL adapter
  is no longer used on them either). Background pause still N119's open question.
- **A browser tab with audio running in a match** (gesture given): the headless run had a suspended AudioContext.
- **Offline play by hand:** only the title screen ran unscripted (now through the port's stream callback at 48 kHz).
