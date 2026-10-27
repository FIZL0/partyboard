# Online play: design

## Model: lockstep, checkpoints for everything else

Rollback needs a savestate taken every frame. Party Board's game state is not in one place: MEM1 heaps,
plus `.data`/`.bss` of `dol.so` and every REL `.so` (dlopen'd), plus libco coroutine stacks (malloc).
So we use **delay-based lockstep** (opentennis's lockstep mode, `play/netplay.rs`): every peer runs the
same frames with the same four pads. Mario Party is turn-based on the board and tolerates 2-4 frames of
input delay in minigames.

Recovery uses the game's own **board checkpoint**: after each minigame `resultDll` calls `SLSaveBoard`,
which serializes GWSystem + GWPlayer[4] + GWPlayerCfg. That blob plus the session seeds is a full
restart point. It is used for: continue a saved match, rejoin after an app restart, desync recovery.

## Pieces

- `net/` Rust crate `pbnet` (staticlib, C ABI in `include/port/pbnet.h`). Transport, lobby, lockstep,
  recover. Ported from opentennis `crates/hst-base/src/net.rs` without Bevy. Game-agnostic: inputs are
  opaque per-frame bytes, the hash is computed by C.
- `src/port/netplay.c`: the C side. Pad injection, frame gating, deterministic time and seeds, hash,
  checkpoint save/load, match start.
- `src/port/ui/online.cpp`: RmlUi menu.

## Hook points (from the code survey, 2026-10-27)

- Loop: `game_main` `src/game/main.c:117-204`, one tick per loop, `frame_limiter` in `src/port/imgui.cpp:256`.
- Pads: `PadReadVSync` `src/game/pad.c:154`, `PADRead(status)` at `:160` fills `_Pad*`; `HuPadRead`
  copies to `HuPad*[4]`. `PADStatus` in `extern/aurora/include/dolphin/pad.h:92`.
- Players: `GWPlayerCfg[4]` (`include/game/gamework_data.h`), set in `src/REL/mentDll/main.c`.
- Randomness: `frand` reseeds from `OSGetTime()` when 0 (`src/game/frand.c:10`); `BoardRandInit`
  (`src/game/board/main.c:1429`) seeds from `OSGetTime()`; `rand8` is a fixed LCG. Other OSGetTime
  users: bootDll, `src/game/audio.c`, `saveload.c:384,838`, ztardll.
- Audio is stubbed (no effect on state). DVD reads are synchronous.
- Party setup: `BoardPartyConfigSet`, `BoardSaveInit` (`src/game/board/main.c:281,374`).
- Checkpoint: `SLSaveBoard`/`SLLoadBoard` `src/game/saveload.c:393,581`; written after each minigame in
  `src/REL/resultDll/main.c:1018`.
- Match start: `netplay_match_start` (`src/port/netplay_match.c`): canned state at the loop top before frame 0,
  a top-priority process calls the board in frame 0, free heap memory zeroed when the board overlay starts
  (`omWatchOverlayProc` hook). Findings: `research/journal/2026-10-27-n8-match-start`.
- Match end: a menu overlay (modeseldll) starting after the board marks it over (`netplay_ovl_start`); at that frame's
  `netplay_frame_end` `pbnet_lockstep_finish` (drains what peers still need), `netplay_end`, `pbnet_lobby_back` (room open
  again, new match id) and the UI shows the lobby. Findings: `research/journal/2026-10-27-n25-match-end`.
- Online saves / continue: `netplay_checkpoint` (`src/port/netplay_save.c`, hooked in `resultDll` where `SaveExec`
  would write the card) -> `pbnet_save_write` (`net/src/save.rs`) -> `<user dir>/online/<match id>.json`; continue is
  `Host::resume` (`net/src/lobby.rs`, setup `"resume"`) + `netplay_match_continue`. Findings:
  `research/journal/2026-10-27-n9-online-saves`.
- Drops (N10): `net/src/recover.rs` (rejoin token in `<user dir>/rejoin.txt`, host's `Book`, reload room, epoch tag on
  lockstep datagrams), wired in `lobby_c.rs` (guest redials mid-match, host `rejoin`/`host_reload`/`stand_in`, reload
  held via `lockstep_c::hold` until `pbnet_recover_go`); `netplay_lobby_poll` starts each epoch's room (continue from
  its checkpoint); `update_online_match` (`src/port/ui/online.cpp`) shows the hold. Findings:
  `research/journal/2026-10-27-n10-drops`.
- Desyncs (N11): `netplay_tick_begin` hands each `pbnet_poll_desync` mismatch to `pbnet_desync_recover`
  (`net/src/desync.rs`): first one per session writes `<user dir>/desync-*.json` (`Session::input_log`), the host
  reloads the last checkpoint via `host_reload`. Findings: `research/journal/2026-10-27-n11-desync-recovery`.
- Resync (N46): never runs on desynced, never refused. Resync points at every player's turn start (`MainFunc` ->
  `netplay_turn_checkpoint`, `src/port/netplay_resync.c` -> `pbnet_resync_point`, memory only); a turn point resumes
  that turn (`BoardStartCheck` answers yes while the board is created, `netplay_turn_resuming`). Repeats from one point
  hold 2/4/8/16 s first (`desync::Resync::delay`, `pbnet_resync_poll`). "Resyncing with <host>..." toast. Debug:
  `--desync-at <frame>`. Whole-state transfer was spiked and is not possible yet. Findings:
  `research/journal/2026-10-27-n46-desync-resync`.
- UI: RmlUi documents in `src/port/ui/`, config in `src/port/config.cpp`, user dir from SDL pref path.

## Rules

- No game data in git (no disc files, no extracted assets).
- Gameplay code under `src/game` and `src/REL` stays as decompiled; netplay changes there are small
  hooks guarded by `if (netplay_active())`, offline play must be unchanged.
- Never end a match on a drop. Hold, offer CPU stand-in or save-and-quit, keep the save on every peer.
