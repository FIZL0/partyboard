# N50: `--turbo` for scripted runs

Every in-game check so far (N15, N25, N26, N30, N33) patched `src/game/main.c` or `netplay_debug_init` by hand to
set `disableFrameLimiter`, and put `"video.enableVsync": false` in the run's config.json, because a hidden window
with vsync blocks on present (~10 fps). Neither patch was committed, so the journals' runs were not reproducible
as written.

## What changed

- `netplay_debug_init` (src/port/netplay.c) parses `--turbo`. Without `--script` it is logged
  (`netplay: --turbo needs --script, ignored`) and dropped.
- `netplay_debug_turbo()`: true for a loaded `--script` run with `--turbo`, and false while lockstep runs (a
  lockstep session keeps the 60 Hz pace its peers share).
- `src/game/main.c`: the frame limiter is skipped when `disableFrameLimiter` (TAB key) **or**
  `netplay_debug_turbo()`; lockstep still always limits, as before. Without the flag nothing changes offline.
- `src/port/portmain.cpp`: aurora is initialised before `netplay_debug_init`, so `netplay_debug_turbo_arg(argc, argv)`
  scans argv for `--turbo` + `--script` and opens the window with `config.vsync = false` for that run only (the
  setting is not changed or saved). With this a fresh user dir needs no config.json edit.
- `tools/netplay/tick_gate_test.c`: `turbo_checks` (argv scan, ignored without --script, off under lockstep).

## Usage

    XDG_DATA_HOME=$PWD/run/xdg HOME=$PWD/run \
      timeout -s KILL 60 build/linux-default-relwithdebinfo/partyboard \
      --script tools/netplay/mash-a.script --hash-log run/h.log --turbo

## Checks

- Two runs in parallel, fresh user dirs (vsync on by default), `mash-a.script`, 40 s KILL timeout:
  without `--turbo` 69 hash-log lines (frames) in 40 s; with `--turbo` all 17400 frames (the script's `end`), reaching
  the board and its turn-1/turn-2 autosaves, and the process exited by itself before the timeout.
- `--turbo` alone (no `--script`): logs the "ignored" line, runs normally.
- `tools/netplay/tick_gate_test.sh`: passes, with the new turbo checks.

## Not verified

- `--turbo` with `--script` in a real lockstep session (two peers): the code path is `!netplay_lockstep()` in
  `netplay_debug_turbo` plus main.c's existing `|| netplay_lockstep()`; only the unit test (fake pbnet lockstep)
  checks it. A two-peer netns run was not worth its cost for a one-condition guard.
- Other platforms (Android/iOS/Windows): no toolchains here; the change is plain C/C++ with no platform code.
- Whether the frame-limiter-free run gives the same hash log as a paced one: runs are not deterministic anyway (N53),
  so I did not compare them.
