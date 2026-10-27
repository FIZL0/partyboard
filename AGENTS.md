# Agent rules

Party Board: PC/Android/iOS port of the Mario Party 4 decomp (C, CMake, aurora for GX, RmlUi menus).
We are adding online play. Design and hook points: `plan/NET.md`. Netcode reference to port from:
`~/repos/opentennis` (Rust; read, do not edit).

- Build: `cmake --preset linux-default-relwithdebinfo && cmake --build --preset linux-default-relwithdebinfo`.
- Gate: `tools/check.sh` (cargo test in `net/` if it exists, then the CMake build). Must pass before `done`.
- The game needs a US disc image (GMPE01). If `orig/GMPE01_00/` has none, anything that has to run the
  game is checked by its code and tests only: say so in the commit and block the run part with
  `done --blocked "needs disc image"` rather than guessing.
- Never commit disc files, extracted assets or anything under `orig/*/`.
- Offline play must be unchanged: netplay hooks in `src/game` and `src/REL` are `if (netplay_active())`
  guards, kept small.
- Rust in `net/`: stable toolchain, no `unsafe` beyond the C ABI layer, every `extern "C"` fn documents
  its safety contract.
- Write findings worth keeping (what made two peers diverge, what the earliest common state is) to
  `research/journal/<date>-<ID>-<topic>/README.md`.
