# N8: match start

## What was done

`netplay_match_start(const NetplayMatch *m)` (`src/port/netplay_match.c`, API in `include/port/netplay.h`):
every peer, from whatever menu it is in, enters the Room's board in the same state, and lockstep frame 0 is
the first frame of the match.

- `NetplayMatch` is the Room as the game needs it: seed, board 0..5, turns, bonus, teams, and per seat
  character, cpu, CPU difficulty, team, handicap. N20/N7 fill it from the Room of the lobby's `Start`
  (same on every peer). `netplay_match_invalid` checks it (the limits match mentDll: turns 10..50 by 5
  (`main.c:4826`), handicap 0..9 (`:4918`), 8 unique characters, team battle = 2 vs 2).
- **Order of events** (one peer; all peers do the same at the same lockstep frame):
  1. UI calls `netplay_match_start` on the game thread, outside the tick. It calls `netplay_begin(seed)`
     (frame counter, clock and the three seeds from the Room's 64-bit seed folded to 32 bits) and marks the
     match pending. No game code runs until the next loop.
  2. Next loop, `netplay_tick_begin` first calls `netplay_match_tick`, **before** frame 0's hash and
     `pbnet_step`. It overwrites everything the hash covers and the unhashed state the board reads:
     - `HuPadNetplayReset()` (new, `src/game/pad.c`): `HuPad*` and pad.c's statics (`_PadBtn`,
       `_PadRepCnt`, `_PadDStkRepCnt` ...) to idle connected pads. Without it frame 0's `HuPadRead` hands the
       game the local controller state of the last menu frame, and the repeat counters differ per peer.
     - GWSystem, GWPlayer, GWPlayerCfg zeroed, then what modeseldll (`main.c:192-196`) and mentDll
       (`fn_1_B654`, `main.c:1880-1934`) set for a party: explain on, show COM minigames on, minigame list "all",
       message speed normal, `BoardPartyConfigSet`, GWPlayerCfg (`pad_idx = seat`, the lockstep pad),
       groups from the Room's teams, `BoardSaveInit(board)`. Zeroed GWSystem also clears flag (1,0), so the
       board starts fresh (`BoardMGSetupPlayClear`: the dol-static "recently played minigames" lists, which
       would differ after one peer played a party offline earlier).
     - **GWGameStat swapped for a canned one** (`GWGameStatDefault` + all minigames available, w06 open,
       very hard CPUs unlocked, custom pack off, English): the board reads the save file (`mg_setup.c:368`
       `GWMGAvailGet`, `main.c:636` customPackEnable, `pause.c:915` veryHardUnlock, `roll.c:173`
       language). The player's own is restored at `netplay_end` (an online match leaves no trace in the
       local save; online saves are N9).
     - Save mode 1 ("don't save"): `resultDll` then never runs `SaveExec` against the local card.
     - Wipe reset to a still blank in the board's colour (white, black for board 5 like mentDll): a wipe
       left running would make the board's `WipeCreate` a no-op on one peer only (`WipeCreate` returns
       while `stat != 0`). `GlobalCounter = 0`, `HuSysVWaitSet(1)`.
     - Queues `MatchStartProc` at priority 0xFFFF.
  3. Frame 0: `MatchStartProc` runs before any other process, so no process of the menu overlay runs again
     with per-peer state. It does what mentDll's `fn_1_C174(3)` does minus the wipe-out
     (`CharModelKill`, `MGSeqKillAll`, `CharMotionInit` x4, audio fade), sets the overlay history to the
     one a menu-started party has (boot(1) -> modesel(1) -> mstory3dll, mentDll's own entry renamed as
     mentDll does at `main.c:1966`) so the board returns alike everywhere, and `omOvlCallEx(w01 + board)`.
     Doing this inside a process, not from the loop top, matters: `CharModelKill`/`HuWinAllKill`/... call
     `HuPrcKill`, and some audio paths `HuPrcVSleep`, which must run on a process's coroutine.
  4. When `omWatchOverlayProc` starts the board (new hook in `src/game/objmain.c`, behind
     `netplay_active()`), `netplay_ovl_start` zeroes the data of every free block of every heap and logs
     each heap's layout hash (`HuMemHeapWalk`, new in `src/game/memory.c`).
- mentDll itself is not changed: the match start mirrors its path instead of calling into it (the menu's
  functions depend on its own bss state and on the menu objects being alive).
- Debug: `--match <board>@<frame>` (with `--script`) starts a match (1P human, 2P-4P CPU, 10 turns) after
  that frame of a scripted run; the hash log has a new `heap=` field (`netplay_heap_hash()`, layout of all
  heaps' used blocks); `tools/netplay/hashdiff.py --from FRAME` compares from the match on and reports a
  heap-only difference as a note.

## Earliest common state: what I found (from the code; not run, see below)

- **What is not reset by an overlay load**: `omOvlKill` frees only blocks with `num == MEMORY_DEFAULT_NUM`
  in the SYSTEM, DATA and DVD heaps. Blocks allocated with `HuMemDirectMalloc` (`num = -256`, e.g. process
  heaps, wipe copies) or other nums (data dirs, MUSIC/MISC heaps) survive; so does all dol `.data`/`.bss`
  (pad statics, mg_setup's play history, wipe, ARAM motion cache `charWork[].motAMemP`, `GWGameStat`).
  So **a fresh overlay load is not enough by itself**; the list above is what the match start resets on
  top of it.
- **Heap layout**: the allocator (`src/game/memory.c`) merges a freed block with free neighbours, so a heap's
  layout is a function of its set of used blocks. After the menu overlay is freed, peers' heaps are
  laid out alike iff their surviving blocks (non-default nums, leaks) are the same. They need not be the
  same: a menu path that leaks a `-256` block, or a data dir one peer loaded, leaves a different layout.
  Addresses are not game state (no hashed struct holds a pointer), so a different layout only matters if
  the game (a) reads memory it did not write, or (b) runs out of heap on one peer. (a) is handled by
  zeroing free memory at board start; (b) is not handled. The `heap=` field / the per-heap log line at
  board start show whether layouts differ in practice.
- Not reset because it is presentation only: ARAM contents/layout, data dir cache, the 3D/sprite/window
  pools (killed by `omOvlKill`), rumble.

## Tests

- `tools/netplay/match_test.{c,sh}` (run by `tools/check.sh`): `netplay_match.c` with the real
  `src/game/memory.c` and stub game functions, under ASan/UBSan. Validation limits; start folds the seed and
  starts the session once, a second start is refused, an invalid one is refused; nothing happens before
  the tick; the tick sets BoardPartyConfigSet's arguments, GWSystem/GWPlayer/GWPlayerCfg (stale values
  cleared), the canned GWGameStat, the wipe, and queues the process at 0xFFFF, once; the process sets the
  history and calls `w01 + board`; free heap memory is zeroed only when the board overlay starts, used
  blocks keep their data and the layout hash is unchanged; `netplay_match_end` restores the player's
  GWGameStat; teams map to groups, board 5 wipes black; `--match` parsing.
- `tools/netplay/tick_gate_test.c` got stubs for the new calls in `netplay.c`; still passes.
- `hashdiff.py --from` and the heap note checked by hand on two toy logs.
- `tools/check.sh` passes; `libdol.so` exports the new functions; no new warnings in the touched files.

## Tried / failed

- First plan did the overlay switch directly in `netplay_match_tick` (loop top, outside any process). Dropped
  before building: `CharModelKill` -> `HuPrcKill`, and `HuAudDllSndGrpSet`'s `HuPrcVSleep`, use
  `processcur`/`processthread`, which are only valid inside `HuPrcCall`.
- Considered calling `netplay_begin` again for `--match` in a script run: it would restart the frame count
  under a running script (script lines and `end` are by frame). A script run keeps its session instead.

## Not verified

- **No game run** (no GMPE01 image in `orig/GMPE01_00/`): nothing here ran in the game. In particular
  not checked: that the board starts and plays from this state; that killing the menu from a process at
  priority 0xFFFF is clean from every menu the online UI can sit on (title, modesel, mentDll); that the
  still blank wipe is followed by the board's own wipe-in; that two peers with different menu paths give
  identical hash logs from frame 0; whether their heap layouts differ. How to check with a disc: run
  `tools/netplay/mash-a.script` twice with `--match 0@900 --hash-log`, once with a script that wanders
  through other menus before frame 900, then `hashdiff.py --from 900`; read the `netplay: heap N:` lines.
- **No two-peer run**: nothing calls `netplay_match_start` with a real Room yet (N20 glue / N7 menu not
  landed); the Room -> `NetplayMatch` conversion is theirs.
- Language forced to English (1): fine for the US disc only build this targets; not checked against a
  non-English GwLanguage setting.
- After the match (results -> mstory3dll -> modesel) lockstep keeps running and `netplay_end` is never
  called: match end is filed as a follow-up.
- Heap exhaustion on one peer only (different surviving blocks) is not handled; only observable with a run.
- Windows/Apple builds not built (no SDKs); the new code is plain C with no platform parts.

## Follow-ups filed

- **N23** match end (stop lockstep, `netplay_end`, back to the lobby).
- **N24** the disc run of this match start (identical state from the match frame, heap layouts).
