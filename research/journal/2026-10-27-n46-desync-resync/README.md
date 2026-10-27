# N46: desync resync, never run on desynced

**What landed: the fallback (plan/N46.md item 3), not whole-state transfer.** A desync now always ends in a resync.
Everyone reloads the host's newest resync point, which is the start of the current player's turn. Before this it was
the last post-minigame save, up to a whole turn back, and after 3 reloads the match ran on desynced. Inside a
minigame the resync still goes back to the start of the last turn before it. It does not go to the minigame's start
(see Not verified / follow-up).

## Spike: whole-state snapshot (item 2), and why it was not built

I measured with gdb on two `--script ... --match 0@900` runs (`set disable-randomization off`; gdb turns ASLR off by
default, and my first pair of runs had identical addresses because of that). Both breaks were at
`BoardPlayerTurnExec(0)` on w01:

| state | run 1 | run 2 |
|---|---|---|
| `MEM1Start` (aurora malloc) | 0x7fe4cbfff010 | 0x7fed03fff010 |
| `HeapTbl[0..4]` (HuMem heaps, `OSAlloc` = `malloc` on PC) | 0x7fe4f196d010 ... | 0x7fed146ff010 ... |
| `libdol.so` base | 0x7fe524600000 | 0x7fed5a200000 |
| `w01Dll.so` base (dlopen'd REL) | 0x7fe5100bf000 | 0x7fed38044000 |
| current process (`HuPrcCurrentGet`) | 0x7fe4f1977890 | 0x7fed14709890 |

- The heaps are 9 + 5 + 42 + 22 + 63 MB = **~141 MB** (`HeapSizeTbl` x4 on PC, plus the rest of the 24 MB arena).
  They are full of absolute pointers: process lists, models, HSF data, and `omObjData` function pointers into
  `libdol.so` and the REL `.so`. Every base differs between processes. The offset from base to pointer is the same
  (`...010` endings), so relocating would need to know which words are pointers. There is no type info for that.
- To make the addresses equal you would need: the heaps `mmap`ed at a fixed address (doable), `libdol.so` and every
  REL at fixed bases (glibc `dlopen` cannot choose a base; Android has `android_dlopen_ext` with a reserved region; a
  custom loader or a static build would be needed), libco stacks (`co_create` mallocs them outside the heaps)
  allocated in a fixed region, and aurora's GPU-side textures/buffers rebuilt for heap data that changed underneath
  them.
- Even in one process, restoring at N+300 breaks as soon as a coroutine or REL is created or freed in between: the
  restored heap would point at freed libco stacks or a REL mapped elsewhere.
- Conclusion: whole-state transfer is a project of its own (fixed-address loader + allocator), not a step of this
  task. I did not build the snapshot code. Decision made unattended, written down here as the plan asked.

## What was done

**Never runs on desynced, never refused** (`net/src/desync.rs`, `net/src/lobby_c.rs`):
- `Resync::allow` (max 3) is replaced by `Resync::delay`. The first resync from a point goes at once. Each further one
  from the *same* point holds first for 2, 4, 8, then 16 s (cap). This is the rate limit for a deterministic bug
  that a reload can't fix. The match keeps resyncing instead of running on desynced.
- During the hold the host's game is held (`hold`). The guests stop within the input delay because they lack the
  host's pads. `pbnet_resync_poll` (new, called from `netplay_lobby_poll` every loop) fires the held-back reload once
  the wait is over.
- `pbnet_desync_recover` writes the input log once per session as before, but now asks the lobby every time. Repeats
  are refused while a resync is pending or held.
- A guest does **not** hold itself on its own mismatch. It only marks "resyncing". If it held, it would stop sending
  pads/hashes, and the host might never get the hash it needs to see the mismatch (deadlock). The host sees every
  mismatch (N11's argument), so the guest runs at most until the host's hold or Reload reaches it.
- `Status` has `resync` ("Resyncing with <host's seat names>...", empty otherwise) and `resyncs` (the host's count).
  Both are cleared at `go` (the new session runs).

**Resync points at every turn start** (`src/port/netplay_resync.c`, hook in `src/game/board/main.c` `MainFunc`):
- Right after `GWSystem.player_curr = i` (the normal, non-continued turn path), `netplay_turn_checkpoint` takes the
  `SLSaveBoard` blob. During a lockstep match it hands it to `pbnet_resync_point`, which stores it as
  `recover::LAST` (`point_taken`), the same slot `host_reload` reads. It is memory only: nothing goes to disk, so
  N47's per-turn autosave can write files without overlapping.
- **A turn blob did not resume the turn at first.** The first `--continue` from a player-1 blob started at
  **player 0**, because `BoardPlayerInit` (`player.c:239`) resets `player_curr` and every player's space when
  `BoardStartCheck()` is false, and `BoardPlayerModelInit` clears items. The game only keeps them on a return
  mid-round (flags 1,2..1,6: after a minigame, a Bowser/battle/fortune event).
- Fix: `BoardStartCheck` answers yes (`netplay_turn_resuming`) while the board is created from a turn blob: armed by
  `netplay_match_continue`, the board already started (flag 1,1), no event flag. `MainFunc` disarms it right after
  `CreateBoard` (`netplay_turn_resumed`), so `ExecTurnStart` and the loop see the real flags and run turn `i`
  normally. Post-minigame saves (flag 1,2) and match starts (no 1,1) are unaffected.

**UI** (`src/port/ui/online.cpp`): when `resync` becomes non-empty, a 3 s toast shows "Online match / Resyncing with
<host>...". There is no modal, no quit and no lobby.

**Debug flag** `--desync-at <frame>` (`src/port/netplay.c`): once per process, at that frame of a lockstep session,
this peer XORs its frand seed with 1.

## Tests

- `desync::tests::a_forced_mismatch_resyncs_and_play_goes_on_with_equal_hashes`: fake wire (30 ms lag, 2% loss),
  host + guest. The guest flips its state at frame 500. The host sees the mismatch and resyncs from its last turn
  point on a new epoch; the guest takes it 80 ms later. After 1500 frames both peers' hashes are equal. Then the bug
  is made to recur every session: 4+ resyncs happen, the gaps between them grow, the last is >= 4 s, and the host
  never runs a frame of a session it knew was desynced.
- `desync::tests::a_resync_never_is_refused_and_repeats_back_off`, `the_resync_text_names_the_hosts_seats`.
- `lobby_c::tests::a_desync_resyncs_everyone_and_repeats_wait_longer` (replaces N11's "until the reloads run out"),
  with two real iroh endpoints:
  - The guest's desync only sets the text.
  - The first resync goes at once; the 2nd and 3rd wait 2 s and 4 s (`resync_poll` false before, true after).
  - The text clears at `go`.
- `tools/netplay/tick_gate_test.c`: `pbnet_resync_poll` runs on every loop of a started lobby, and never before.
- `tools/check.sh` passes.

In-game with `$PB_DISC` (each run with its own HOME/XDG under the scratchpad, vsync off):
1. **Offline unchanged**: `--script tools/netplay/mash-a.script --hash-log` (13200 frames: boot, party setup, w01
   turns, instDll) on this branch, and on the pre-task sources (e92430cd's `board/main.c`, `netplay.c`,
   `netplay_match.c`, `netplay.h`, `files.cmake`, rebuilt in the same build dir). The two hash logs are
   **byte-identical**, ending at hash 49E98D41456DA884.
2. **Turn resume**: I made a player-1 turn blob with gdb (`--match 0@900`, player 0 set to CPU at its turn, then
   `call netplay_checkpoint()` at `BoardPlayerTurnExec(1)`, frame 4043). Two `--continue` runs then each log "the
   board resumes at player 1's turn". Turns 1, 2, 3 and the minigame overlay follow, and the two 12000-frame hash
   logs are **identical**. Player 0's space (50), coins (13) and colour (1) at the resumed turn equal the original
   run's at the same point. Before the BoardStartCheck fix, the same test started at player 0.
3. **Two peers, real lobby, forced divergence**: two processes in a private `Xwayland :97`, Online > Host / Join
   with the long code over the X clipboard, relay off, and the guest started with `--desync-at 2400`.
   - Guest: "frand seed flipped". Both peers: "desync at frame 2430", each writing its `desync-*-e0-f2430-p{0,1}.json`.
   - Host: "everyone resyncs", "reload 1 from the match start". Both then logged "lobby started (epoch 1)".
   - After the resync the two hash logs (cut at the last frame-0 line) are **identical for 3957 frames**
     (hashdiff.py), and no further desync was logged.
   - Screenshot: both boards show the same scene.

## Tried / failed

- First gdb spike pair: identical addresses, because gdb disables ASLR by default. Redone with randomization on.
- A board-turn save through mash-a's tail (A + START every 240 frames) never got past player 0's turn in 12000
  frames. I switched to an A-only script and made player 0 a CPU from gdb.
- First turn-resume run started at player 0 (the BoardStartCheck finding above). Fixed.

## Not verified

- **A two-peer resync from a turn point (not the match start).** In the lobby run the board waited at Toad's intro
  text: no controller is assigned under Xwayland and the lobby has no scripted pads, so no turn started before the
  desync. That resync went to the match start. The turn-point path was only checked through `--continue` in single
  processes (test 2), which is the same `netplay_match_continue` code that the reload uses.
- **The "Resyncing with <host>..." toast was not seen on screen.** The screenshot came after it expired. The JSON
  field is covered by the lobby_c test.
- **The backoff hold in the game** (a second desync from the same point) was not run in-game: `--desync-at` fires
  once per process. Only the Rust tests cover it.
- **Resync to a minigame's start** (plan item 3's second half) is not done. A desync inside a minigame goes back to
  the start of the last player's turn before it (up to about 1.5 min). Resuming into a minigame needs the
  mgsetup/instDll overlay chain started from netplay. Filed as a follow-up.
- Whole-state transfer: spiked (above), not built.
- Windows/Apple/Android not built (no SDKs). `pbnet_stub.c` has the two new functions.
