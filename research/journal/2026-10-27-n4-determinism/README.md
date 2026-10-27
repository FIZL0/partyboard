# N4: determinism in C, `--script` / `--hash-log`

## What was done

- `src/port/netplay.c` + `include/port/netplay.h` (new): session state (`netplay_begin(seed)`,
  `netplay_active()`, `netplay_frame()`), deterministic time, derived seeds, the state hash and the
  debug script mode.
- **Time.** While a session is active `OSGetTime`/`OSGetTick` return
  `2026-01-01 + (seed % 365 days) + frame * 1/60 s`. Each extra call in the same frame adds 25 µs, so
  results stay increasing and busy-wait timeouts still end. `HuAudSndGrpSetSet`'s `SNDGRP_WAIT` loop
  spins on `OSGetTick` without yielding, and a flat per-frame value would hang it. Call counts are the
  same on every peer in lockstep, so this stays deterministic.
- **Port-side override, aurora untouched.** `cmake/netplay.cmake` (included once, after the REL loop in
  `CMakeLists.txt`) links `dol` with `--wrap=OSGetTime,--wrap=OSGetTick,--wrap=PADRead` and every REL
  with `--wrap=OSGetTime,--wrap=OSGetTick`. The RELs then import `__wrap_OSGetTime` from `libdol.so`.
  Offline the wraps call straight through to aurora (`__real_*`). Checked with `nm -D`: `bootDll.so`
  imports `__wrap_OSGetTick` and `modeseldll.so` imports `__wrap_OSGetTime`. `BoardRandInit`
  disassembles to `call __wrap_OSGetTime@plt`.
- **Seeds.** `netplay_seed(salt)` = mix32 of the session seed, the salt and the current frame (never 0).
  `netplay_begin` sets `frand_seed`, `rnd_seed` (rand8) and `boardRandSeed`. Hooks, all
  `#ifdef TARGET_PC` + `if (netplay_active())`:
  - `src/game/frand.c`: when frand reseeds (state 0), the session seed replaces `rand8 ^ OSGetTime`.
    The original lines still run first, so `rand8` advances exactly as it does offline. Also added
    `frandSeedGet/Set` (`frand_seed` is static), declared in `port/netplay.h` so `game/frand.h` is untouched.
  - `src/game/board/main.c` `BoardRandInit`: `boardRandSeed = netplay_seed(NETPLAY_SALT_BOARD)`. It runs on
    every board setup, including the return from each minigame. Mixing in the frame gives each call a
    new seed that is still the same on every peer.
  - `src/game/main.c`: `netplay_frame_end()` after `GlobalCounter++`, once per completed game tick.
- **Hash.** It is named `netplay_hash()`, not `pbnet_hash` as the brief says. plan/NET.md says the hash
  is computed by C, and `pbnet_*` is the export prefix of the Rust crate: N1's objcopy keeps only
  `pbnet_*` global. A C function with that prefix would be misleading. When N3/N5 exchange hashes, C
  passes `netplay_hash()` to pbnet. The hash is 64-bit FNV-1a over the raw bytes of GWSystem,
  GWPlayer[4] and GWPlayerCfg[4], then frand/rand8/BoardRand seeds (as little-endian u32), then all
  `HuPad*` arrays including `HuPadErr`.
- **Debug mode** (parsed in `port_main` right after the pbnet log line):
  - `--script <file>` loads an input script and starts a local session at boot with the script's seed.
    `__wrap_PADRead` then returns the scripted pads for all four ports, and real controllers are ignored.
    The format is documented in `netplay.c`: `seed`, `end <frame>`, and lines of the form
    `<frame> <port> <buttons> [sticks triggers]`. A pad state holds until that port's next line. The run
    quits after `end` frames (`PartyBoard_IsRunning = FALSE`). The prelaunch UI is skipped when a valid
    DVD image is configured, so runs need no clicks.
  - `--hash-log <file>` writes `<frame> <hash>` for every frame (line-buffered). On its own, without
    `--script`, it still starts a session (seed 0x50424E54) but the pads stay live.
  - A bad argument (missing or unparsable script, unwritable log) is fatal and names the problem.
  - `tools/netplay/mash-a.script`: an example script that taps A/START for 3600 frames.
- **Headless: not possible, windowed instead.** With `BACKEND_NULL`, `port_main` only runs the
  prelaunch UI loop and returns, so the game never starts. Changing that would touch boot flow outside
  this task.

## Tried / failed

- First smoke run used `timeout 8` with output piped to `grep`. The game ignores SIGTERM, so it ran
  until the tool timeout and the grep printed nothing. `timeout -s KILL` with output written to a file works.

## Verified

- `tools/check.sh` passes (2 cargo tests, desktop build links).
- Standalone harness: `netplay.c` compiled with stub game globals and a fake game loop that reads the
  pads, calls OSGetTime 3×/frame and mutates GWPlayer from the seeds. It was not committed, because the gate
  has no C test runner. Results:
  - Two runs of the same script give byte-identical 120- and 3600-line hash logs.
  - Changing the seed changes the hash from frame 0.
  - Scripted pads land on the right frames (frame 10: port 0 `A+START`, stick -5, port 1 `0x0200`; frame 60:
    port 0 released, port 2 `B+X`).
  - Time strictly increases. `OSGetTime` passes through (`__real_`) before a session starts.
  - A port out of range or pad lines out of frame order are rejected with file:line.
- Real binary, no disc: `--script` logs `script ...: 4 pad lines, seed 00001234, end 120` and
  `session start` at boot, then waits in the prelaunch UI (no disc), so no frames run. A missing script
  aborts with `[FATAL] bad --script/--hash-log argument`.

## Not verified

- **The task's check, "two runs of one script give identical hash logs", in the real game: BLOCKED,
  no GMPE01 disc image in `orig/GMPE01_00/`.** Without a disc `game_main` never runs, so not one real
  frame has been hashed. Follow-up N15 has the run and script tuning.
- Whether other nondeterminism is left in the game (uninitialised struct padding in GWSystem/GWPlayer,
  heap addresses leaking into state, `VIGetRetraceCount`, which is a stub returning 0, audio stubs,
  threads in `sreset.c`). These only show up in a real two-run diff (N15).
- Whether `mash-a.script` gets past the boot/title screens or does anything useful: never run against a disc.
- Apple and Windows: `--wrap` is ELF-only, so cmake/netplay.cmake skips it there with a STATUS message.
  On those platforms netplay time stays the wall clock and `--script` pads are not injected
  (`PB_NETPLAY_WRAP` is unset, so `__wrap_PADRead` is not built). Follow-up N14. Android uses
  lld/ELF and should work, but has not been built here.
- `PADRead` injection for real lockstep (pbnet pads) is N5's job. `__wrap_PADRead` in `netplay.c` is a
  natural place for it and would avoid editing `src/game/pad.c`.
- Offline play unchanged: covered by code reading only. Every hook is behind `netplay_active()`,
  which is false unless `--script`/`--hash-log` is passed, and the wraps then call `__real_*`. Not run
  in-game (no disc).
