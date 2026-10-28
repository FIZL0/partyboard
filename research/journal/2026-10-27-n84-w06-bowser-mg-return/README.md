# N84: two-peer run of w06's Bowser minigame return (flag 1,6)

N77 checked this path by reading the code only. This task runs it with two peers. A human player on w06 passes Bowser's
spot as a mega player, plays m457 or m458, and comes back with flag (1,6). On the way back, `fn_1_AB4C` must set
`itemPrev` again from `board_data` (`unk06`) on every peer. The task also covers a resync and a continue that land
inside the minigame.

## Outcome

- **Any mega player who reached Bowser's spot hung the board in the port, offline too, so the minigame never started.**
  - In `fn_1_A408` (w06 `bowser.c`), the player is thrown up and the screen wipes out. The code then waits on
    `HuAudFXStatusGet(<jump sound 0x3D>) != 0`.
  - The port has no sound effects. `src/port/audio.c` stubs `HuAudFXPlay`, which returns 5, and `HuAudFXStatusGet`,
    which returns 12 ("playing") for every sound.
  - So the wait never ended. Upstream fixed the same kind of softlock for streams in b1c33527 (`HuAudSStreamStatGet`
    12 -> 0).
  - The wait comes before the COM check, so a CPU mega player hung the board the same way.
  - A one-process `--match 5@300` run with the scratch force (below) shows it. Before the fix, it sat in the board
    from the wipe at frame 4656 to the script's end at 17400. After the fix, it plays m458 and returns at 7055.
- **Fix:** under `TARGET_PC`, `fn_1_A408` skips that wait. The next line, `HuAudFXAllStop()`, stops every sound
  anyway.
  - I did not change the `HuAudFXStatusGet` stub. Its other callers only check the status, they do not wait on it:
    the board's taunts, w01/w03 `mg_coin.c` and m439. Returning 0 would change their offline behaviour and hashes.
- **The return works with two peers.** After the fix, I ran plain, desync and Continue runs, with m457 and m458 both
  played. On both peers, `fn_1_AB4C` sets `itemPrev` from `board_data` (`unk06 - 1`). The move end then reads the
  restored value on both peers. The hash logs are identical in every run.
- **The restore really does the work.** In one run the guest's `itemPrev` is overwritten with 9 before the restore,
  and the logs stay identical. In the control run the guest also skips the restore, and the peers diverge (table).

## Harness (`harness/`, scratch, not committed)

- **`instrument.diff`** is the scratch diff as it was during the runs. It also holds the fix hunk.
  - **Pads:** N56's `n38_pads`. This machine's seat taps A every 24 frames, and START in instDll and the party results.
  - **Lobby:** N56's `n56_harness.inc` is included in `online.cpp`.
  - **Force (w06 `bowser.c`):** with `PB_N84=<turn>` (default 1), the first human player whose walk event runs in that
    turn gets Bowser's event. `fn_1_92D8` treats the space as Bowser's, `BoardPlayerSizeSet(p, 2)` makes the player
    mega, and `itemPrev` becomes `PB_N84_ITEMPREV` (default 1, a Mega Mushroom, which is what a real mega player has).
    - The marker is `GWSystem.board_data[16]`. w06 uses bytes 0..7. The marker is in the blob, so a resync or a
      continue to the turn point forces the event again.
    - When the board returns with (1,6), the same space counts as Bowser's once more. `fn_1_CA8C` would refuse it,
      since the space is not type 10, and then the restore would never run.
  - **Logs:** `n84:` lines when `itemPrev` is saved into `unk06`, when the minigame overlay is picked, when the restore
    runs (before and after values), and when the move ends (`BoardPlayerTurnMoveExec`, the `itemPrev` it reads).
  - **`PB_N84_SCRAMBLE=<n>`:** sets this peer's `itemPrev` to n just before the restore.
  - **`PB_N84_NORESTORE`:** skips the restore on this peer.
- **`n56_harness.diff`:** N77's `PB_N77_BOARD` patch is applied first, so the host picks w06. This diff adds the
  Continue host's save button label (`PB_N84_SAVE`, here `Bowser*`).
- **`pair.sh <tag> <secs>`:** a host and guest pair on w06 in a private netns. `GARGS`/`HARGS` add arguments for one
  peer, and `GENV` adds environment for the guest only.
- **`killcont.sh <tag>`:** N56's kill and Continue. The host is SIGKILLed 10 s into Bowser's minigame and the guest
  leaves. The host then relaunches and hosts the w06 save from the Continue tab, and the guest joins it.
- **`offline.sh <tag> [args]`:** a mash-a `--script --turbo --hash-log` run in its own user dir.
- To compare hash logs, I used N77's `lastsession.py` on each log and then `tools/netplay/hashdiff.py`.

## Runs

The setup for every run:
- GMPE01 Rev 1 (`$PB_DISC`), `build/linux-default-relwithdebinfo`, Linux.
- A private netns per run, relay off, vsync off.
- Room w06, 20 turns, 2 humans + 2 CPUs.
- The forced player is the first human in turn 1.

"Identical" means `hashdiff.py` found every field equal over the last session.

| run | what | result |
|---|---|---|
| a2 | plain | player 0 forced at 3187. `unk06` = 2 (`itemPrev` 1). **m458** 3563–5618. Back with (1,6). Restore `1 -> 1` on both peers. Move end reads `itemPrev 1` on both and runs the Mega Mushroom's restore. Then two turns and two minigames (ovl 16, 10). **Identical for 17497 frames** |
| a3 | plain | **m457**, player 1. Restore `1 -> 1` on both. **Identical for 11490 frames** |
| a4 | plain | m458, player 1. **Identical for 11490 frames** |
| b1 | guest `--desync-at 4500`, inside **m457** | Desync at 4500, everyone resyncs. Both peers continue from player 1's turn point (epoch 1, frame 17). The force fires again at 283, m457 again 656–2338, back, restore `1 -> 1` on both, move end `itemPrev 1` on both. Turn 1 ends, minigame 9, turns 2 and 3. **Epoch 1 identical for 16578 frames**, no further desync |
| kc1 | host killed 10 s into **m458**, Continue tab | The guest leaves. host2 hosts the w06 save (turn 1, player 0, written at 2741) and the guest joins. Both continue from player 0's turn. The forced event replays, but this time the draw is **m457** (657–2323). Restore `1 -> 1` on both, then turn 1, a minigame and turn 2. **Identical for 13850 frames** |
| sc1 | guest's `itemPrev` set to 9 before the restore | guest: restore `9 -> 1`; host: `1 -> 1`. Move end reads `itemPrev 1` on both. **Identical for 12691 frames** |
| nr1 (control) | guest's `itemPrev` set to 9 and its restore skipped | guest's move end reads `itemPrev 9`. **First difference at frame 6066** (`frand`: the guest skips the Mega Mushroom's restore effect). `plr0` differs at 6156. Equal again from 6298, the next turn point's reseed. pbnet's lockstep check (every 30 frames, on the gameplay hash) saw none of it |

### A resync or continue "into the minigame"

No point is ever taken inside m457/m458, by design (N52). The minigame point (`netplay_minigame_point`) is only taken
for flag (1,2), the end of the round. The turn checkpoint in `MainFunc` does not run while (1,6) is set. Bowser's
minigame is also called straight from the board (`BoardNextOvlSet`), so it goes through neither instDll nor resultDll,
and resultDll's checkpoint never runs. So a desync inside it (b1), or a continue after a quit inside it (kc1), goes back
to the start of that player's turn. That turn point's blob has `unk06` 0, since the last restore cleared it, and has no
(1,6). The turn plays the event again, and `board_data` brings the same `unk06` to every peer.

`itemPrev` can only differ between peers at the return if a peer entered between the save and the restore. No point
exists there, so `sc1` stands in for that case. The restore then makes the peers equal.

The draw between m457 and m458 (`BoardRandMod(100)`) came out differently after the Continue (kc1: m458, then m457).
The continued session has its own seeds. Both peers drew the same one.

## Offline

`offline.sh` is the mash-a script with `--turbo --hash-log`, with the fix and without it (HEAD's `bowser.c`):
- w01 party: byte-identical hash logs over 17400 frames, ending on `AE36457C2B93C6C0` (N78's value).
- `--match 5@300` (w06): byte-identical over 17400 frames, ending on `5C1768A61F3852F5`.

Neither run takes the mega-at-Bowser path, which is the only code the fix changes. With the force, the fixed one-process
run (s4) plays m458 and returns.

`tools/check.sh` passes (run by `done`).

## Tried / failed

- The first two-peer run (a1) hung right after the forced event. Step logs through `fn_1_A408` led to the sound wait
  (above).
- The first fixed run showed no restore: the board came back and went on to the next player. On the return,
  `fn_1_92D8` checks `fn_1_CA8C` (space type 10) again, so the forced space, which is not Bowser's, skipped
  `fn_1_93C8` and with it the restore. The force now counts the return too (the marker goes 1 -> 2). A real Bowser
  spot is type 10, so this was a harness artefact, not a game bug.

## Not verified

- **A real mega player reaching Bowser's spot**, with a Mega Mushroom used in a turn and a walk past the spot: the
  harness forces it. The forced call goes through the same `fn_1_92D8` -> `fn_1_93C8` -> `fn_1_A408`, from the walk
  event, as the real one. The only differences are the space type and the player's position: the player is thrown
  from space 3, not from next to Bowser.
- **`itemPrev` other than 1:** a Super Mega Mushroom (3) also passes through the restore, but I ran only 1, plus 9 as
  the scramble.
- **A COM mega player at Bowser** (`fn_1_A408`'s COM branch skips the minigame and restores at once): I did not run
  it. It went through the same sound wait, so it hung before the fix too.
- **The (1,7) branch** (Bowser wins, the player loses half their coins). The runs only took the branch the minigame
  result gave, and I did not record which one.
- **A desync during the return itself** (after the minigame, before the move ends). Its resync point is the same
  turn start as b1's, but I did not force it there.
- **The other `HuAudFXStatusGet` callers** (taunts, w01/w03 `mg_coin.c`, m439) see "playing" for ever in the port.
  They do not hang, but they may behave differently from the GameCube (for example, a taunt sound slot never freed).
  That is out of scope here, and I did not look into it. Filed as N85.
- More than two peers, a real network, Windows/Android/Apple: not run (no SDKs, one machine). The fix is plain C under
  `TARGET_PC`.
