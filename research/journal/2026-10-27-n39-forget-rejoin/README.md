# N39: forget the rejoin record when an online match ends

Before: a guest's `<user dir>/rejoin.txt` (N10) stayed after the match ended (N25), so for `recover::FRESH` seconds
Online > Join kept offering "Rejoin Match" for a finished match. Only "Forget Match" / "Leave Match" in the UI
removed it.

## What changed

- `src/port/netplay.c` `match_finish` (the N25 end of a non-scripted match, same frame on every peer): calls
  `pbnet_rejoin_forget()` after `netplay_end()`, unconditionally, so it also happens when the lobby closed meanwhile
  (no `pbnet_lobby_back`). The host has no record; the call is a no-op there.
- `net/src/lobby_c.rs` `Shared::back` (what `pbnet_lobby_back` runs): forgets the record too, under the lobby lock.
- `net/src/lobby_c.rs` guest keep tick: now holds the lobby lock while it checks `phase == Started` and calls
  `recover::keep`. Before, it read the phase, dropped the lock, then rewrote the record; a tick that read the record
  just before the C-side forget could write it back (keep re-saves what it read). With the lock held and the
  forget in `back` (which flips the phase to Open under the same lock), a forgotten record can't come back.
- Not changed on purpose: N34's `match_left` (a match left mid-way, or the host saved and quit) keeps the record —
  that guest may still rejoin.

## Tests

- `tools/netplay/tick_gate_test.c` (in check.sh): fake `pbnet_rejoin_forget`; the N25 match-end check asserts one
  forget at match end, a second one for the no-lobby end, and none on any N34 mid-match leave.
- `recover::tests::a_record_is_kept_and_forgotten` already covers forget + keep not resurrecting.

## Not verified

- No lobby-level Rust test that `Shared::back` deletes the file: the record dir is a process global
  (`recover::USER_DIR`) and the lobby tests run in parallel, so a test setting it would make every other
  test's guest write records into it (flaky). Making the dir per-lobby would be a refactor outside this task.
- No in-game two-peer check (play a party to its end, then look at Online > Join): a full online party to the results
  takes far too long scripted, and lobby matches in these slots desync at frame 1080 (N55).
- Windows/Apple/Android builds (no toolchains here).
