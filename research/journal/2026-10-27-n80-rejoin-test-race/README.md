# N80: deflake lobby_c::tests::a_restarted_guest_rejoins_by_token_and_both_reload

## The race
The failure was always at the stand-in step (`lobby_c.rs`, `assert_eq!(host.epoch(), 2)` after
`host.stand_in(1)`), with epoch 3 seen. It came up in 3 of 30 runs on the unchanged code.

`Shared::stand_in` moves the host to reload 2 and sends `Msg::Reload` to every guest. The guest is still
connected, so its conn task sees it is no longer seated and right away sends `Msg::Rejoin`. The host gives the seat
back with reload 3. That is the designed sequence: two reloads, and the test then waits for epoch 3. The test
read the epoch under a second lock, though, after `stand_in` had released the first one. On loopback the round
trip from Reload to Rejoin to reload 3 sometimes finished in between, so the test saw 3. **It is not a double
reload in the code.** The keep-alive/away timer suggested in the brief is not involved.

## Fix
The test does the stand-in and reads the epoch under the same `lock(&host.shared)` critical section, the same
work `Lobby::stand_in` does plus the read. Epoch 2 is now seen for sure, because the host's tasks cannot handle
the Rejoin while the test holds the lock. The code itself is unchanged.

## Checks
- Before the fix: 3 failures in 30 runs (`left: 3, right: 2` at the stand-in assert).
- After: 60/60 runs in a row via `cargo test --lib a_restarted_guest`, plus 100 more as 4 concurrent loops of 25
  (CPU contention), 0 failures.
- `tools/check.sh` via `run done`.

## Not verified
none
