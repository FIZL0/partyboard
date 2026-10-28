# N113: web_pair presses START at every instDll

Brief: `tools/web_pair.mjs` pressed START at a minigame's instructions only after a `resync point ... minigame`
line. A battle/Bowser minigame (mid-turn, flag (1,3)/(1,4)) prints no minigame point, and neither does an instDll
resumed after a reload (`match.scrub` skips `netplay_minigame_point`). Both stood at instDll for ever (N102 d1/d2/m1).
A `--desync-at` flip that landed in instDll was then never detected, because instDll is not hash-checked (N64) and
the minigame never started.

## Change (tools/web_pair.mjs only)

- A `Start New OVL 3 ` console line (instDll, `include/ovl_table.h` index 3) arms the START timer the same way the
  minigame point did. The line has no frame, so the timer is set to `-2`, and the main loop's next second replaces it
  with the tab's current `pbnet_frame()`. That is up to one second late, which the 300/700-frame delays absorb. A
  minigame point printed right after the OVL line (`netplay_ovl_start` runs after objmain's OSReport) still sets the
  exact frame. After a reload the frames restart. The resumed instDll's OVL line resets the timer to the new frame,
  so a stale point frame from before the reload does not block it.
- A new summary line, `desync-at: b: flip at F, desync at frame G (a)`, reports each peer's `--desync-at` flip and
  the first `netplay: desync at frame` at or after F in *any* tab's log. Only the comparing peer (the host here)
  logs the desync line. The flipped guest only logs `pbnet: reload 1`. My first version searched only the flipped
  tab's own log and wrongly printed `NO DESYNC` for run i1. I fixed it and replayed the fixed logic on both runs'
  logs (build/n113/sum.mjs, a copy of the same code).

The verdict is unchanged: a planted desync still FAILs the run (desync lines), as before.

## Runs

These ran in the browser against build/web from 6cd815b (one commit behind HEAD; the newer commit only adds N105's
m411 viewport patch). Settings: `PAIR_SEED=0x18E2A0AF4FE4E301`, `PAIR_CPUS=2,3`, `--hash-log`. Outputs are in
build/n113/ (not in the repo).

| run | b's flip | result |
|-----|----------|--------|
| d1 | `--desync-at 16500` (N102 d1's) | Turn 2's instDll (275.5 s) printed no minigame point, a battle/Bowser game. START came anyway and OVL 12 started at 284.2 s. Turn 3's instDll (501.5 s, no point) went on to OVL 46 the same way. The flip at 16500 fell in OVL 12's play and was caught at 16500. Reload 1 brought it back to turn 2's last turn point, and the match ran on to turn 3 with 5 minigames, 19704 frames. FAIL only for the planted desync |
| i1 | `--desync-at 7150`, inside turn 1's instDll (point at 7097) | The flip landed in instDll and nothing was flagged there. START came, m-game OVL 22 started, and host a caught the desync at 7590 (OVL 22's first checked frame). Reload 1 went to the minigame point. The resumed instDll printed no point, got START again, and OVL 22 ran. The match then reached turn 3 at 12245 frames. FAIL only for the planted desync |

So a flip in instDll is detected, but only once the minigame runs (~440 frames later in i1). That is N64's design: instDll may
run a frame apart on two peers. The harness change is what lets the minigame start at all.

## Not verified

- **An instDll resumed after a reload *mid-minigame* with N102's m415 forcing (m1's exact setup).** i1's reload
  resumes into the same instDll path (OVL 3 after a scrub, no point), which is the code path m1 stood in. I did not
  rerun m1 because it needs N102's instrument build.
- **PAIR_MENU, PAIR_DROP and 3-4 tab runs.** The START logic is per tab and does not depend on how the lobby
  started. I didn't rerun those modes.
- **The `mg-explain off` script setting** (instDll without instructions). START there is the same as before for
  minigame points. I did not run it.
- **Desktop harnesses.** They are out of scope. This task touches only the browser script.
