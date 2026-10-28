# Agent rules

Party Board: PC/Android/iOS port of the Mario Party 4 decomp (C, CMake, aurora for GX, RmlUi menus).
We are adding online play. Design and hook points: `plan/NET.md`. Netcode reference to port from:
`~/repos/opentennis` (Rust; read, do not edit).

- Build: `cmake --preset linux-default-relwithdebinfo && cmake --build --preset linux-default-relwithdebinfo`.
- Gate: `tools/check.sh` (cargo test in `net/` if it exists, then the CMake build). Must pass before `done`.
- The game needs a US disc image (GMPE01). `PB_DISC` (set in every slot) is its absolute path; the game reads
  it at boot instead of the saved setting. Run real in-game checks with it (`--script`, `--hash-log`,
  `--match`). Slots run games in parallel: give each run its own user dir (`XDG_DATA_HOME`/`HOME` under
  the worktree) so config.json and saves don't collide.
- Game windows: slots set `SDL_APP_ID=partyboard-test`, and the desktop opens that class floating at 1280x960 on a
  hidden workspace, so run the game directly (no gamescope/Xwayland tricks, never on the user's screen).
- At most 2 game instances per slot at a time (desktop games and browser tabs together; a two-peer pair is the
  most a test needs). Repeat runs go one after another, never fanned out: 11 parallel games froze the user's PC.
  A 3-4 peer run is the only exception, and nothing else of the slot runs beside it.
- Targets: Linux, Windows, Android, iOS, browser. macOS and tvOS are not targets: don't build, test or fix
  for them (leave upstream's files for them alone).
- Never commit disc files, extracted assets or anything under `orig/*/`.
- Offline play must be unchanged: netplay hooks in `src/game` and `src/REL` are `if (netplay_active())`
  guards, kept small.
- Rust in `net/`: stable toolchain, no `unsafe` beyond the C ABI layer, every `extern "C"` fn documents
  its safety contract.
- Write findings worth keeping (what made two peers diverge, what the earliest common state is) to
  `research/journal/<date>-<ID>-<topic>/README.md`.
