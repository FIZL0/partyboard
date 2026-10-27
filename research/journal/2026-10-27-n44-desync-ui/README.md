# N44: desync UI

Task: show "Out of sync, going back to the last save point" while a desync reload runs, and tell players when the
per-checkpoint reloads ran out.

## What "ran out" means now

N44 was filed against N11, where the host reloaded a checkpoint at most 3 times and then let the match **run on
desynced**. N46 removed that cap. The reloads never run out now. A repeat desync from the same save point (one a
reload can't fix) makes the host hold the game for 2/4/8/16 s before it tries again (`desync::Resync::delay`). That
hold replaced "ran out", so it is what the players are now told about: going back did not fix it, and the game
tries again in N seconds.

## What changed

- `net/src/desync.rs`: `resync_text(room, host)` ("Resyncing with <host>...") is replaced by
  `RESYNC_TEXT` = "Out of sync, going back to the last save point..." and `held_text(secs)` = "Still out of sync
  after going back to the last save point. Trying again in N seconds...".
- `net/src/lobby.rs`: new `Msg::Held { secs }`, host → all. Before this change, only the host knew about a hold;
  guests only saw their game stall because the host's pads stopped arriving. Peers of one match run the same build
  (the lobby checks `ident.build`), so adding a message variant is safe.
- `net/src/lobby_c.rs`: `Shared::held_s`. The host sets it and sends `Held` when `desync()` holds. A guest sets it
  (and `resyncing`) when `Held` arrives. `begin_reload` clears it, so the next reload shows the "going back" text
  again. `status()` fills `resync` with one text or the other, plus the new `Status::held_s`.
- `src/port/ui/online.cpp`: the toast already showed `resync` whenever the text changed. It now stays up
  3 s + `held_s`, so the hold notice lasts as long as the hold.
- `plan/NET.md`: N46's line names the new texts.

## Tests

- `desync::tests::the_texts_say_what_happens` (replaces the host-name test).
- `lobby_c::tests::a_desync_resyncs_everyone_and_repeats_wait_longer` (two real endpoints on a local testnet): the
  guest's own mismatch shows `RESYNC_TEXT`. On every repeat, the host's status shows `held_text(2|4)` with
  `held_s`. The guest gets `Msg::Held` over the wire and shows the same text. Once the reload goes, the host
  is back to `RESYNC_TEXT` and the guest's `held_s` is 0.
- `tools/check.sh` passes (cargo test + CMake build, including online.cpp).

## Tried / failed

Nothing failed.

## Not verified

- **The toast on screen.** I didn't do an in-game two-peer run. The game's `--desync-at` flips the seed only once
  per process, so it can't produce the *repeat* desync from the same point that a hold needs. Showing the hold
  in-game would need a debug flag that flips the seed in every session, plus the N38 in-game button presser
  (`research/journal/2026-10-27-n38-drops-in-game/harness`) to drive the lobby. The "going back" toast is the
  same N46 toast path with a new string. N46's two-peer run exercised that path.
- **The toast length on screen** (3 s + hold): I didn't check it on screen, for the same reason.
- **Windows/Apple/Android builds**: no toolchains here. The change is portable Rust/C++ (`std::max` was already
  used in online.cpp).
