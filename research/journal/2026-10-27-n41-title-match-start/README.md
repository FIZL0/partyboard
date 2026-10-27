# N41: match start from the title screen (bootDll)

## The reported crash was already fixed by N26

N25 saw `--script tools/netplay/mash-a.script --match 0@900` segfault right after bootDll.so was unlinked. N25's branch
(fbe4cbb9) does not contain N26's fix (44ade807, `git merge-base --is-ancestor` says no). N26 fixed this exact crash:
`MatchStartProc` kills the menu overlay's own processes (`HuPrcKillIf` + `overlay_process`), so bootDll's `BootExec`
no longer runs on in the unloaded DLL. On this tree the same command logs `killed 1 menu processes` and enters w01 at
frame 901. Nothing of bootDll survives the kill.

## A second crash: a match before bootDll's system init

Match frames swept over the whole of bootDll (mash-a, 0@f for f in 2, 60, 200, 400, 600, 750, 830, 900, 960, 990,
each run 90 s): all reached the board and kept running, except **0@2**: SIGSEGV in the board's first frame:

```
HuAR_ARAMtoMRAMNum (aMemP=0) armem.c:279 <- HuAR_ARAMtoMRAM <- HuWinInit window.c:170 <- BoardWinInit <- CreateBoard
```

bootDll does the game's one-time setup (`CharInit`, `HuWindowInit`, `MGSeqInit`, then `SystemInitF = 1`) after its
first wipe-in and the 3 s logo wait, at frame ~60 or later (0@60 still needed it). On PAL the language menu comes
before it and waits for a choice, so a PAL player can sit before the init for as long as they want. A match started
before the init has `winAMemP == 0` (no window data in ARAM). 0@60 happened not to crash, but it ran without the init
too (chrman effect ARAM, minigame messages).

Fix (`src/port/netplay_match.c`, `MatchStartProc`): if `!SystemInitF`, run `CharInit(); HuWindowInit(); MGSeqInit();
SystemInitF = TRUE;`, as bootDll's `BootInitForSkippedSequence` (TARGET_PC) does. Its `HuWinInit(1)` is left out
because the board calls it. `HuAudSndGrpSetSet(0)` is also left out: it is a no-op in `src/port/audio.c`. The init
runs after the menu kill and before `CharModelKill(-1)`, so that call sees initialised `charWork` (static zeros would
be model 0, not NONE). Logged as `netplay: match start: system init done for bootDll`. I chose this over refusing a
match before modeseldll: a lobby match starts whenever the host starts it, and a refused start on one peer would
leave the lockstep without that peer.

`tools/netplay/match_test.c`: the first start (SystemInitF 0) runs the three inits once and sets the flag; the
continue later (system up) does not run them again.

## In-game checks (GMPE01 at $PB_DISC, own user dir per run, vsync off)

- 0@1, 0@2, 0@10, 0@20, 0@30, 0@40, 0@60: all log `system init done` and enter w01 at f+1 (0@1 enters at frame 1).
  All run 90 s without a crash (~5300 hash-log frames each). 0@900 does not need the init and enters at 901.
- Determinism across the init: 0@20 from bootDll before its init (default config), compared with 0@20 from modeseldll
  (`backend.skipBootSequence`, init done at boot). `hashdiff.py --from 20`: identical for 5288 frames.
- 0@1 (no overlay started yet, killed 0), default config compared with skipBootSequence: identical for 5311 frames.

## Not verified

- **PAL**: the long pre-init window (the language menu) was not run. There is only a US disc (GMPE01). The code path
  is the same `!SystemInitF` check.
- **Two real peers via a lobby** with one before the init: not run. That needs two processes and the Online menu
  within bootDll's first ~60 frames (or a PAL language menu). The hashdiff pair above stands in for it.
- 0@1 enters the board at frame 1 and every later frame f at f+1. Both 0@1 runs agree, but a peer with no overlay
  yet and a peer in a menu would differ by a frame. This is only reachable in the first frame after launch, before
  any UI could start a lobby match, so I did not look into it.
- Heap layouts after the late init differ from a peer that did it at boot (ARAM/heap order). The hashed state is
  identical (above), and N26 already found that heap layouts differ between peers.
