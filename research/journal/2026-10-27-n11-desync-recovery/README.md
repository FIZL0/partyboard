# N11: desync recovery

A hash mismatch now writes each side's input log and has everyone reload the host's last checkpoint. Before this
change it only logged one OSReport line, and the match ran on desynced.

## What was done

**Input log** (`net/src/lockstep.rs`):
- `Session` keeps the pads of every frame it ran (`ran`), capped at the last `LOG_KEPT` = 18000 frames (5 min,
  about 1.2 MB).
- `Session::input_log()` returns an `InputLog`: me, peers, seats, delay, the next frame, the first logged frame,
  one string per frame (the four seats' 12 wire bytes in hex), and the session's own hashes (the last 256).
- A new session starts at each epoch (N10), so the log always runs from that epoch's frame 0, unless more than
  5 minutes have run.

**`desync-*.json`** (`net/src/desync.rs`, C ABI `pbnet_desync_recover`):
- `netplay_tick_begin` passes every `pbnet_poll_desync` mismatch to `pbnet_desync_recover`.
- Only the first mismatch of a lockstep session writes a file (`lockstep_c::input_log_once`, a flag on `Live`).
  Each later hash interval would report another mismatch, so without the flag every 30 frames would write a file
  until the reload.
- The file goes to `<user dir>/desync-<match id>-<unix time>-e<epoch>-f<frame>-p<line-up index>.json`, or the
  current directory when no user dir was set (a `--match` debug run).
- It is pretty-printed JSON, one frame per line, so `diff` on two peers' files shows the first frame where they
  ran different pads.
- "Both sides" comes for free: each peer compares the other's hashes with its own (`compare` runs on whichever side
  comes first, and the test `a_forged_hash_is_reported_as_desync_on_both_sides` shows both see it). So both write
  their own log. No log is sent over the wire.

**Reload** (`lobby_c::Shared::desync`, `Lobby::desync`):
- Only the host decides. It calls N10's `host_reload`: `Msg::Reload` with `reload_room(room, last_checkpoint)`,
  which is the host's own last `SLSaveBoard` blob (or the match start if there is none yet), on epoch + 1.
- `netplay_lobby_poll` then restarts every peer from it exactly as for a rejoin reload.
- **Why host only, with no new guest→host message**: the host compares its hashes with every peer's. If two guests
  differ from each other, the host can't agree with both, so the host itself sees every mismatch. A guest only
  writes its log and waits. This also means no `Msg` variant was added (no protocol change).
- Refused while a reload is pending or before the lockstep runs (`inbox` none), so a mismatch found on both sides
  or twice gives one reload.
- **No reload loop**: `desync::Resync` allows `RELOADS_PER_CHECKPOINT` = 3 desync reloads from one checkpoint (keyed
  by its blob; `None` for the match start). A desync that comes back from the same checkpoint three times is a
  deterministic bug a reload can't fix. After that the host logs "no more reloads from it, the match runs on", and
  the next checkpoint (after the next minigame) starts the count again. Decision made unattended: endless reloads
  would look like a hang, and ending the match is what the task rules out.

## Tests

- `lockstep::tests::the_input_log_has_the_pads_run_and_the_hashes`: two peers on the fake wire log the same pads.
  Frame 0 is idle, frame 2 carries the first own pad, the hashes are listed, and a solo session keeps only the
  last `LOG_KEPT` frames.
- `desync::tests`: `Resync` runs out per checkpoint and starts over on a new one. A `Report` is written and read
  back (file name sanitising, hex hashes, log fields).
- `lobby_c::tests::a_desync_reloads_everyone_until_the_reloads_run_out`: two real iroh endpoints in-process.
  - Before the start, `desync` is refused, and a guest's `desync` is always refused.
  - Each host `desync` moves both peers to the next epoch with `reloading` set and no checkpoint (the match start).
  - It is refused while the reload is pending; `go` starts the new sessions.
  - The fourth desync from the same start is refused, and the epoch stays at 3.
- `tools/netplay/tick_gate_test.c`: every queued mismatch reaches `pbnet_desync_recover` with its fields.
- `tools/check.sh` passes: 70 cargo tests (2 ignored), both C harnesses, the full build.
- Offline in-game run: `--script tools/netplay/mash-a.script --hash-log` with `$PB_DISC`, using its own HOME/XDG
  under the scratchpad and vsync off. It ends at frame 3600 with hash CE198F8A1A3BDCA2, the same as N10's run, so
  offline play is unchanged.

## Tried / failed

Nothing failed. One idea was considered and dropped: a guest→host `Msg::Desync` as a second path to the reload.
It is not needed, because the host sees every mismatch (see above).

## Not verified

- **No two-peer in-game desync.** The online lobby still has no command-line host/join (N35 is the open two-process
  check), and nothing can force a real divergence on demand. Not checked in the game:
  - that a desync-triggered reload mid-board or mid-minigame brings both peers back in step. That is the same
    `netplay_match_continue` path as N10's reload, also not checked in the game.
  - the size and time of writing an 18000-frame log on the game thread. It is about 1.2 MB of pretty JSON,
    written once per session, so a hitch of a few ms is expected but was not measured.
- The UI shows nothing special for a desync reload. `Status.reloading` is set, but `update_online_match` only
  shows a modal when there is a `message`. The game simply goes back to the checkpoint. A short "Out of sync,
  going back to the last save point" notice would be nicer; not done, to keep the change small.
- When the reloads run out, the match runs on desynced, and the players are not told.
- Windows/Apple/Android not built (no SDKs). The stub `pbnet_stub.c` has the new function.
