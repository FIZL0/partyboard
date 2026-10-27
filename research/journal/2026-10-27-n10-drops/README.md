# N10: drops never end a match

## What was done

**Keep-alive and "Waiting for <name>"** (`net/src/lockstep.rs`): each `Session::step` already sends every peer a
datagram, also while it waits, so that is the keep-alive. A peer still running is heard every frame. New
`Remote::heard` / `Session::quiet_ms` / `Session::silent` measure silence on the step clock. `recover::waiting` lists
peers silent for `SILENCE_MS` = 3000 by name (the names of their seats). The lockstep holds by itself, because it
has no pads from them. Nothing in pbnet times out or abandons.

**Same app, new connection: resume on the same frame.**
- Guest (`lobby_c::run_guest`): the connection is now a loop. After the start, a failed dial or a lost connection
  sets `host_lost` and dials the same code again every 2 s from the same endpoint. On reconnect it sends
  `Msg::Rejoin { token, intact: true, epoch }` and points its `Routes[0]` at the new connection.
- Host: `Wire` no longer holds the datagram senders captured at the start. It holds a shared `Routes`
  (`Arc<Mutex<Vec<Option<Sender>>>>`) that the lobby can re-point. `Book::rejoin` returns
  `Outcome::Resume { index }` when the token's seats are still that line-up index's and the epoch matches. The host
  re-points `Routes[index]` (and renames the seat and line-up entry if the endpoint changed) and replies
  `Msg::Resumed`.
- No input log has to be sent explicitly. Every session keeps its own frames until each peer acks them
  (`acked`) and resends from the ack on every step. Once datagrams flow again, the missing frames go out (capped
  per datagram, so it takes a few frames) and the match goes on from the frame where it held.
- Connections are counted per peer name (`conns`). The old connection ends late, when QUIC notices, and must not
  remove the outboxes of the new connection from the same endpoint.

**State lost: everyone reloads the last checkpoint.**
- A restarted app reads `<user dir>/rejoin.txt` (`recover::Rejoin`: code, match id, token, time). The online menu's
  Join tab shows "Rejoin Match" (`pbnet_lobby_rejoin`). `Lobby::rejoin` dials and sends `Rejoin { intact: false }`.
- In that case the host gives the token's seats to the new endpoint and calls `host_reload`. It takes
  `recover::reload_room`: the started room with `checkpoint` = the last checkpoint this process saved
  (`recover::checkpoint_written`, hooked in `pbnet_save_write`). It sends `Msg::Reload { room, epoch + 1 }` to every
  connected guest and calls `begin_reload` itself.
- `begin_reload` (host and guests) sets the room and epoch, drops the inbox, sets `reload_pending` and calls
  `Holder` (`lockstep_c::hold`): `pbnet_step` returns WAIT and `pbnet_lockstep_active` stays 1 until the next
  `lockstep_c::start`.
- C (`netplay_lobby_poll`) starts the room again whenever `pbnet_recover_epoch()` moves while the lobby is started.
  With `pbnet_lobby_checkpoint` > 0 it calls `netplay_match_continue(blob)`, else `netplay_match_start` (no
  checkpoint yet means the match restarts from its start with the same seed). Then it calls `pbnet_recover_go(epoch)`,
  which builds the new session (frame 0, the new line-up) and releases the hold.
- Each epoch's lockstep datagrams carry the epoch's low byte in front (`recover::tag`/`untag`, in `Wire::send` and
  `Shared::datagram_in`; the host relays them unchanged). A datagram of the old session still in flight is never
  read as the new session's: an old ack would have made a peer believe frames were delivered, and it would stall
  forever.
- If an intact guest missed a reload while away (its epoch is old), only it gets the current `Reload`
  (`Outcome::Join`). The others are waiting for it at that epoch's start.

**CPU stand-in, take back, save and quit, host left.**
- Host: `pbnet_recover_cpu(index)` calls `recover::stand_in`: the peer's seats become `cpu`, peer `"cpu"`, name
  `"<name> (CPU)"`, and they keep their key. Then everyone reloads.
- The player takes the seats back when it rejoins with its token (another reload, with the name restored).
- A stood-in guest that is still connected also gets the Reload. Its room has no seat for it, so it sends
  `Rejoin { intact: false }` at once and gets its seats back. The other choice was to leave it hanging: its lockstep
  waits on a host whose new session no longer has it, so it could never come back without a drop.
- In C, a reload room without a local seat is waited out and never `pbnet_lobby_leave`d (it was left before only at
  epoch 0).
- Save and quit is `pbnet_lobby_leave` on the host. Every peer already has the last checkpoint's online save
  (N9). `run_host` now sends `Leave` after the start too.
- A guest that gets `Leave` after the start, or whose host connection is lost, shows `recover::HOST_LEFT`:
  "Host left: the match is saved, any player can host Continue". On a lost connection it keeps redialing and clears
  the message if the host answers.

**Rejoin token**, kept like opentennis `net/rejoin.rs`:
- The host issues one random 128-bit token per guest at the start (`Book::issue`), from std `RandomState` keys
  plus the clock, so no new crate. It goes only to that guest (`Msg::Token`, `To::Peer`). Seat keys go to everyone
  in the Room, so they can't serve as proof.
- The guest writes `<user dir>/rejoin.txt` (JSON) and rewrites it every `KEEP` = 60 s while the match runs
  (`recover::keep`). A record is fresh for `FRESH` = 12 h: the match holds as long as it takes, and an older one is
  from a host that has long quit.
- The host never writes one: there is no other host to rejoin.
- The user dir comes from the menu: `pbnet_recover_set_dir(PartyBoard_ConfigPath)`.

**UI** (`src/port/ui/online.cpp`):
- `update_online_match()` runs from `ui_update` (compat.cpp). It polls `pbnet_recover_json` every 10th loop and
  shows a modal while the status has a message:
  - host waiting: "Keep Waiting", "CPU Plays for <name>", "Save and Quit"
  - guest waiting: "Keep Waiting", "Leave Match" (the rejoin record stays)
  - host left: "Leave Match" (it forgets the record)
- The modal closes by itself when the message changes or clears. "Keep Waiting" hides it until the status changes.
- The Join tab has "Rejoin Match" / "Forget Match" when a record exists and no lobby runs.

**Side effect, intended**: `netplay_lobby_poll` now starts a room that has a checkpoint with
`netplay_match_continue`. Before this change it always called `netplay_match_start`, so a Continue lobby (N9) would
have started a new party. The reload needs this path, and it is the same code.

## Tests

- `net/src/recover.rs` (fake wire, `lockstep::fake` with a new `Net::cut`):
  - **`reconnect_resumes_on_the_same_frame`**: three peers, 50 ms lag and 5% loss, 300 frames, then peer 2's link
    is down for 660 frames. Nobody runs a frame. After 3 s peers 0 and 1 see 2 as silent and 2 sees both as silent;
    0 and 1 keep each other alive. When the link is back all resume and run 500+ more frames. Logs are identical,
    no frame is skipped or run twice (log length == `frame()`), and the replayed state is equal.
  - **`a_lost_peer_rejoins_and_everyone_reloads_the_checkpoint`**: a checkpoint at frame 300 (the folded state),
    then peer 2's app is gone. The host's `Book::rejoin(intact: false)` gives `Reload`, and the seat goes to the new
    endpoint. The reload room has the checkpoint. Every peer starts a new session at epoch 1 from the checkpoint
    state while the epoch-0 datagrams are still in flight; one peer takes the reload 30 frames late. 700+ frames
    run in step, with identical states replayed from the checkpoint.
  - Book: intact resume, plus a new endpoint renamed in place with the line-up order kept. A missed reload gives
    `Join`; an unknown token, or the host's name, is refused. Stand-in keeps the key, and a take-back restores the
    name and line-up. Tokens go to guests only and differ. Epoch tag/untag. Waiting names. Rejoin record
    fresh/stale/future, kept and forgotten. Reload room checkpoint choice.
- `net/src/lobby_c.rs` **`a_restarted_guest_rejoins_by_token_and_both_reload`**, two real iroh endpoints
  in-process:
  1. Start and run 60 lockstep frames. The token arrives privately, and the host's status says "Waiting for a".
  2. The guest's lobby is dropped. A new `Lobby::rejoin` from a new endpoint with the token gets its seat back at
     epoch 1. Both are `reloading`, and the host's game was held. No session starts until `go(1)` (`go(0)`
     refused).
  3. The new sessions run 60 frames in step on the new connection.
  4. `stand_in(1)` makes epoch 2, and the still-connected guest takes the seat back at once (epoch 3, name
     restored). `stand_in(0)` is refused.
  5. The host leaves, and the guest's status says `HOST_LEFT`.
- `tools/netplay/tick_gate_test.c`: every start calls `pbnet_recover_go` with its epoch. A new epoch starts the room
  again, through `netplay_match_continue` when `pbnet_lobby_checkpoint` has a blob, once per epoch. A reload room
  with no local seat is waited out and never left.
- `tools/check.sh` passes: 63 cargo tests (1 ignored, the mDNS one, as before), both C harnesses, the full build.
- In-game, offline only: `--script tools/netplay/mash-a.script --hash-log` with `$PB_DISC` (own HOME/XDG under the
  scratchpad) boots and ends normally at frame 3600 (hash CE198F8A1A3BDCA2). The per-loop hooks
  (`netplay_lobby_poll` epoch read, `update_online_match`) do nothing without a lobby.

## Tried / failed

- First `run_guest` rewrite: the redial `select!` sat inside an arm that still held the `Shared` `MutexGuard` (even
  after `drop`, the generator kept it across the await), so the future was not `Send`. The arm now only sets state
  and breaks out of the message loop. The sleep before the next dial comes after the loop.
- Considered a mid-session CPU stand-in at an agreed frame (merging the absent peer's input log from every peer,
  then idle pads from frame F). Rejected: the game reads who plays a seat (`GWPlayer.com`, the minigame AI set up
  at its start) when a board turn or minigame starts. Flipping it mid-turn or mid-minigame would leave a seat
  nobody plays or break a wait loop, and that can't be checked without a two-peer game run. A reload from the last
  checkpoint is the one path the game already supports (N9's continue). It costs at most the turn since the last
  minigame.
- The test room helper first readied each guest right after its Hello. The next Hello bumped `ver` and cleared the
  first guest's ready, so start failed. Fixed in the helper by readying after all Hellos.

## Not verified

- **No two-peer game run of any of this.** The online lobby has no command-line host/join; it needs clicks in the
  Online menu on two processes (that is N35's open in-game check). Not checked in the game:
  - that the hold modal shows over the WAIT still frame
  - that a reload mid-board or mid-minigame (`netplay_match_continue` called while a match runs, not from a menu)
    re-enters the board cleanly and both peers' hashes agree from the reload frame
  - that `netplay_match_start`'s overlay kill works from inside a minigame
  - what iroh's QUIC idle timeout does to a drop of 3 s or 60 s in practice

  Filed as N36.
- A reload before the first checkpoint restarts the match from its start (same seed). Untested in game.
- A guest that drops while a reload is still pending rejoins as `intact` and gets `Resumed` or the pending reload.
  This is covered by logic only, not by a test.
- The guest can't tell its own network blip from the host dropping, so a guest-side outage also shows "Host left"
  (with `reconnecting: true`) until the redial works.
- Quitting mid-match (host "Save and Quit", guest "Leave Match") stops the lockstep but does not return the game to
  the menu. Ending the match on every peer is N34's (open); until then the leaver's game runs on alone on local pads.
- The rejoin record is not forgotten when a match ends normally, so for up to 12 h the menu offers to rejoin a
  finished match, and the host refuses it. Filed as N37 (after N25).
- The waiting modal's layout (long names, touch) was not looked at on screen; the build compiles and offline boot is
  unchanged.
- Windows/Apple/Android builds not built (no SDKs). `pbnet_stub.c` has the new functions and compiles with
  `-Wall -Wextra`. The stub build as a whole is still N32's.
