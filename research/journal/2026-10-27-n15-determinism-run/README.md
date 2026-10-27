# N15: N4 determinism check against a real disc

## Outcome: blocked, no GMPE01 disc image

`orig/GMPE01_00/` holds only `.gitkeep`, and so do the other `orig/*` slots. I searched the rest of the
machine (`find / -xdev` for `*GMPE01*`, `*mario party 4*`, `*.rvz`, `*.gcz`, and `*.iso`/`*.gcm` under
`~`) and found only other games' images plus Dolphin's `GMPE01.ini`. Party Board's data dir
(`~/.local/share/Party Board`) has no disc configured. Without a disc `game_main` never runs (see the
N4 journal), so no real frame can be hashed. As AGENTS.md says, the run part is blocked rather than guessed.

## What was done (prepares the run, checkable without a disc)

- **Split the hash log by section** (`src/port/netplay.c`, `hash_log_line`). Each `--hash-log` line now reads
  `<frame> <netplay_hash> ovl=<omcurovl> sys= plr0= plr1= plr2= plr3= cfg= frand= rand8= brand= pad=`.
  The first two columns are unchanged, so N4's format and `netplay_hash()` still apply. The extra columns
  answer the task's "which state diverged" from a single pair of logs: they hold per-section FNV-1a hashes
  (GWSystem, each GWPlayer, GWPlayerCfg, all HuPad arrays) and the raw seed values. `ovl` answers "did the
  script reach a board / a minigame" without a screen (board overlays are `OVL_W0x`, minigames `OVL_M4xx`;
  see `include/game/object.h` / the OMOVL enum).
- **`tools/netplay/hashdiff.py A.log B.log`** prints the first differing frame, the last matching frame,
  which sections differ and the overlay at the last match. If the logs are identical it prints the frame
  count and the sequence of overlays reached (`ovl@frame`). Exit 0 = identical, 1 = diverged or length differs.
- `mash-a.script` header now points at hashdiff.py. Its inputs are unchanged: tuning them needs the game
  running (see Not verified).

## How to finish N15 once a disc is in `orig/GMPE01_00/`

```
B=build/linux-default-relwithdebinfo/partyboard   # whatever the binary is called in that dir
timeout -s KILL 120 $B --script tools/netplay/mash-a.script --hash-log run1.log >run1.out 2>&1
timeout -s KILL 120 $B --script tools/netplay/mash-a.script --hash-log run2.log >run2.out 2>&1
tools/netplay/hashdiff.py run1.log run2.log
```
(`timeout -s KILL` and output to a file: the game ignores SIGTERM.) Use the `overlays:` line to tune the
script until it shows a board overlay and then an `M4xx` one, and raise `end` as needed.

## Tried / failed

- Nothing ran in-game (no disc).

## Verified

- `tools/check.sh` passes (cargo tests + desktop build with the new `hash_log_line`, so `omcurovl` links from
  `netplay.c`).
- `hashdiff.py` on synthetic logs: identical logs (rc 0, overlay list `0@0, 2@3`), a log with frame 3's hash
  and `plr0` changed (reports frame 3, last match 2, `plr0 01 vs 02`, rc 1), and a truncated log (reports the
  length mismatch, rc 1).

## Not verified

- **The task's check itself (two real runs give identical logs): not run, no GMPE01 disc anywhere on this machine.**
- **Script tuning to reach a board and a minigame (BoardRandInit, frand): not done.** It needs the game
  running to see where the taps land. I did not guess at menu timings from code; AGENTS.md says to block.
- The new log columns have only been compiled; no real frame has written one (no disc). Their format was
  checked by reading the code and through hashdiff.py's synthetic tests only.
- The N4 open questions (struct padding, heap addresses in state, `VIGetRetraceCount` stub, audio stubs,
  `sreset.c` threads) are still open. They need the two-run diff.
