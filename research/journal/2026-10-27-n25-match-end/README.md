# N25: match end (back to the lobby)

## What was done

After an online party the game leaves the board for the menus (board -> mstory3dll's results -> modeseldll; with
flag (1,27) the board returns straight to modeseldll). Until now lockstep, the deterministic clock and the canned
GWGameStat stayed on in the menus. Now, on every peer, at the end of the same frame:

- **Detection** (`src/port/netplay_match.c`, `netplay_ovl_start`, the existing `omWatchOverlayProc` hook; no new hook
  in `src/game`): once the board overlay has started, the next start of a menu overlay (`modeseldll`, `mentDll`,
  `bootDll`) marks the match over and gives the player's own GWGameStat back right there, before the menu's
  ObjectSetup reads it (modesel reads the save file's unlocks). Minigames, `resultDll`, `mstory3dll` and the board
  coming back after a minigame are not menus. A menu started before the board (the one the match start kills) is
  not the end. `netplay_match_over()` reports it.
- **End** (`src/port/netplay.c`, `match_finish` from `netplay_frame_end`, after the frame counter moved): the overlay
  start runs inside a game tick on a process, so nothing is torn down there; the end of that tick is the first point
  outside the game where every peer is at the same frame.
  - `pbnet_lockstep_finish()` (new): the session stops at once (`pbnet_lockstep_active` 0, so the next loop runs on
    local pads), and a pbnet thread **drains**: keeps sending each peer the own pads it has not acked and the acks of
    theirs, until every peer has this machine's pads up to the last frame, 5 s pass, or a session starts/stops.
  - `netplay_end()`: session off (real clock, local pads), `netplay_match_end` (GWGameStat, checkpoint forgotten),
    still frame dropped.
  - `pbnet_lobby_back()` (new) if the match had a lockstep and a lobby exists; `netplay_lobby_returned()` is then true
    once, and `ui_update` (`src/port/ui/compat.cpp`) opens a `LobbyWindow` with a toast "The match is over. Back in
    the lobby." (`open_lobby_after_match`, `src/port/ui/online.cpp`). A lobby that closed meanwhile (guest lost the
    host) shows its "Lobby Closed" modal from the same window.
  - A `--script` run keeps its session (frames, script, hash log go on) and only ends the match
    (`netplay_match_end`), as the match start already does for script runs.
- **Lobby after the match** (`net/src/lobby.rs`, `net/src/lobby_c.rs`):
  - `Host::reopen(id)`: the started room opens again as a new match id (a new online save file), seed/delay unset,
    nobody ready, a continue's checkpoint and saved seats forgotten; seats (the CPU seats the start filled included)
    and rules stay; `ver` moves, so a guest's stale Ready is answered with the room.
  - `Guest`: a non-started Room that comes while this guest still plays is kept aside (`after`) and does not change
    what it shows; `Guest::back()` takes it, or, if the host was slower, opens the started room locally the same way
    until the host's arrives.
  - `Shared::back` (behind `pbnet_lobby_back`): phase Started -> Open. Host: reopen with id
    `<endpoint id[..8]>-<unix secs>` (the same rule as the first id), and guests whose connection is gone lose their
    seats (`Host::gone` is ignored while started, so a mid-match drop would otherwise hold a seat forever). Guest:
    closes with "Lost the connection to the host." if its connection is gone.
  - The lockstep's line-up and inbox are **kept** until the next start (new `lockstep_on` flag guards the
    once-per-match start instead of `inbox.is_some()`): the finished session's drain still receives on that inbox,
    and the host still relays guest-to-guest datagrams for guests that have not reached the last frame.

## Why a drain (found while writing the test)

Every peer ends after the same frame N, but not at the same real time. A peer that just went silent at N can strand a
peer still behind: that peer's missing pads for frames < N are only resent by the silent one. The lockstep test
`a_finished_peer_drains_until_the_others_reach_the_end` runs 3 peers to frame 300 on a 50 %-loss wire, the last one
60 frames late, 20 seeds: going silent strands a peer on at least one seed (asserted, so the test proves something);
with the drain every peer reaches 300 on every seed with identical pads. The drain is time-capped (5 s) because the
last peer to finish may never hear the acks it waits for (the others already stopped); that costs nothing, it only
runs on a pbnet thread.

## Tests

- `cargo test` (net): the drain test above; `back_in_the_lobby_after_the_match` (lobby.rs: g1 back before the host's
  room, host reopens, g2 still playing keeps the started room, back takes the host's, stale Ready, second match);
  `started_lobby_runs_the_lockstep` extended over three real endpoints: after the 90 lockstep frames b is back first,
  then the host (new id, ver+1, same seats, back once), a keeps the started room while playing and takes the
  reopened one on back, nobody ready, ready again, a second start hands a second lockstep to all three;
  `c_abi_steps_a_solo_session` covers `pbnet_lockstep_finish` (inactive at once, step OFF, twice is a no-op).
- `tools/netplay/match_test.c`: no "over" for minigames/results/mstory3dll/the board again; over at modeseldll with
  the player's GWGameStat back; a later change to it is kept; `netplay_match_end` clears it; a menu before the board
  started is not the end.
- `tools/netplay/tick_gate_test.c`: `netplay_frame_end` with a started lobby and lockstep: finish, session off,
  still frame dropped, lobby back to open, `netplay_lobby_returned` once, next tick on local pads; without a lockstep
  (no lobby) only the session ends.
- `tools/check.sh` passes; `clippy --all-targets` clean.

## In the game (disc at $PB_DISC)

Goal was a scripted `--match` party played to its end, to see `netplay: match over at frame N` and modeseldll come
up with the player's GWGameStat. Not reached; every blocker is outside N25 (filed):

- Runs need their own user dir (`HOME`/`XDG_DATA_HOME` under the worktree) **and `"video.enableVsync": false` in
  that dir's `MarioPartyRD/Party Board/config.json`**: with vsync on and the window on a workspace you can't see,
  presents block and the game runs ~10 fps. For speed I used a temporary, uncommitted `PB_TURBO_TMP` env check in
  `netplay_debug_init` that sets `disableFrameLimiter` (the TAB turbo): ~1150 frames/s with vsync off.
- `--match 0@900` with `mash-a.script` (still in bootDll, the title) **segfaults** right after bootDll.so is unlinked
  (gdb: a jump into unmapped code, no frames). From modeseldll (`mash-a` is there from frame 995) `--match 0@1500`
  enters the board at 1501 (heap lines logged, as N8 wrote). Filed **N38**.
- With A taps every 20 frames the board reaches the minigame explanation (instDll) at 8570 and stays there for
  260k frames: instDll (`main.c:295`) waits for exactly START. Alternating A/START taps instead segfault in the board
  at ~6973 (START opens the pause menu; not looked into). With a temporary `GWMGExplainSet(0)` (uncommitted) the
  first minigame (m421) and resultDll ran, N9's checkpoint was written (`netplay: checkpoint at frame 12481, turn
  1/10`), the board came back at 12543 and then the process aborted with `*** stack smashing detected ***` at
  ~16077. Filed **N40**. The match end path did not run in these: no menu overlay started after the board.

Both temporary changes were reverted; the run's user dir was deleted.

## Not verified

- **The match end in the game**: never reached (above). Unchecked there: that modeseldll comes up cleanly after
  the session ends at the end of the frame its overlay started (real clock and local pads from the next frame),
  that it reads the player's own GWGameStat, and that `mentDll`/`bootDll` are the only other menus a party can
  leave to (a soft reset to the title would be `bootDll`; `mstory3dll`'s story paths go to mentDll but online is
  party only). Filed **N39** for the two-process version.
- **Two peers ending together**: the drain and lobby back are tested over the fake wire (lockstep) and over three
  real in-process iroh endpoints (lobby), not by two game processes; there is no command line way to host/join, it
  needs the Online menu (N39).
- **The lobby window coming back**: `open_lobby_after_match` was compiled, never shown (no match end reached in
  the game, and no way to drive the menu without input injection). Whether it should also pull the menu bar open,
  and how it looks over modeseldll, is unchecked.
- **Drain cap**: 5 s is a guess; a peer whose acks keep getting lost drains the full 5 s (harmless, a pbnet thread).
  A new match starting inside those 5 s stops the drain (EPOCH); old datagrams still in flight then reach the new
  session's inbox. They are dropped by frame range (frames far beyond the new tick), but their ack field is taken
  (`acked.max(ack).min(own_next)`), which could make the new session skip a resend of its first frames. Starting a
  new match needs every guest back and Ready again, so in practice this is many seconds later; not tested.
- **Mid-match leave** (host leaves, a guest's lobby closes during the match): still only stops the lockstep; the
  session does not end. That is the other half of **N34**, whose party-end half this task does.
- Windows/Apple/Android builds: not built (no toolchains); `pbnet_stub.c` got the two new functions and builds in
  the Linux tree (libpbnet_stub.a).

## Follow-ups filed

- **N38** match start from the title (bootDll) segfaults.
- **N39** two-process in-game check of this match end and a second match.
- **N40** a `--match` party crashes before its end (instDll needs START; stack smashing in turn 2; START in the board).
