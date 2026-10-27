# N43: mash-a.script into a minigame and back to the board

## Outcome

`tools/netplay/mash-a.py` now ends port 0's START taps at frame 10900 (the minigame starts at 10907) and
runs to 17400. Two runs of the regenerated `mash-a.script`, in parallel, each from a fresh user dir:
**17400 lines each, `cmp`-identical**, `hashdiff.py` "identical: 17400 frames", final hash
`004C982C415C8583` (frame 17399). Overlays:
`1@1, 74@995, 70@1911, 89@4182, 3@10579, 28@10907 (m420), 84@15554 (resultDll), 89@15892 (w01 again)`.

## What was done

- Tuning run 1: START window 10500-12000 (every 60 frames), END 16000. instDll 10579, m420 (ovl 28)
  started at 10907, and stayed there to 16000: the START taps at 10920+ paused it (as N15 expected).
  So N15's window (last tap 10740) was only one or two taps short: the tap at 10800 or 10860 is the one
  instDll accepts.
- Tuning run 2: window 10500-10900 (last tap 10860), END 22000. m420 10907-15553 with all four ports
  tapping A every 30 frames, resultDll 15554, w01 15892, the next instDll 17801.
- Committed: INST_END = 10900, END = 17400 (past the results and the second BoardRandInit, before the next
  instDll). INST_START stays 10500 (taps during instDll's intro are ignored, and it did not matter).
- Runs used a temporary `if (getenv("PB_TURBO")) disableFrameLimiter = TRUE;` in main.c (N50, not
  committed) and `"video.enableVsync": false` in each run's config.json. They still ran at about 60 fps
  (17400 frames in ~5 min), fast enough; I did not dig into why turbo did not speed them up further.

Run commands (per run `n`; `$U` a fresh dir):
```
mkdir -p "$U/MarioPartyRD/Party Board"; echo '{"video.enableVsync": false}' > "$U/MarioPartyRD/Party Board/config.json"
XDG_DATA_HOME=$U HOME=$U timeout -s KILL 1500 build/linux-default-relwithdebinfo/partyboard \
  --script tools/netplay/mash-a.script --hash-log run$n.log > run$n.out 2>&1
tools/netplay/hashdiff.py run1.log run2.log
```

## Findings

- `frand` changes only once inside m420 (10907-15553) in this run, so m420 exercises little of the
  frand path; `brand` is reseeded at 15894 (w01 + 2, the second BoardRandInit) and advances on the board.
- N53 reports a divergence inside m420 at frame 25497 of a different (`--match`) run. These two runs of
  this script did not diverge, so that cause did not show up on this path.

## Not verified

- Two runs only, same binary, same machine; N53 says runs can diverge, so two identical runs do not
  prove the path is always deterministic.
- A minigame other than m420, and a second minigame (the next instDll at 17801 is past END).
- frand-heavy minigame code: m420 hardly draws frand here (see Findings).
- Why PB_TURBO did not raise the frame rate above ~60 fps (not needed for the task).
