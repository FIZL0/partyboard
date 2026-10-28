# N117: why m444 (ovl 52) never ended in N106's sweep

N106's sanitizer sweep (research/journal/2026-10-28-n106-float-casts) saw m444, the board-event pinball, sit
until the end of the run in three cases: `--match 2@1500` and `5@1500` with party.script (78142 / 85344 frames),
the all-COM w02 boot jump (`FCO_OVL=90`, 38228 frames from frame 111773), and m444 alone with four random-pad
humans (`FCO_OVL=52 FCO_HUMAN=15`, wander.script, all 11996 frames). m444 alone with 4 COMs ended (3795 frames).
N106's sweep ran before N114.

**Answer: N114 already fixed all of it. No code change here.** The wait is in `fn_1_8DD0`'s ball loop: the ball
never reaches a pocket, so `fn_1_B1E8` never returns a hole and the loop never breaks. That isn't a sound-end
wait (N84) and doesn't depend on a human seat. The cause is N114's part 2: on pre-N114 x86-64 builds,
`VECMag((Vec *)&arg1)` read the pointer's own bytes, and with the run's ASLR the ball's gravity could be 0.6 instead
of 0.3. At 0.6 a weak launch falls back onto the plunger and the ball sits there for ever (x 128, y about -98,
its velocity flipping by ±0.3..0.9 a frame). The `--match` runs also had N114's part 1 (party.script's human never
pulled the plunger). The normal build behaves the same: the code is the same, and the sanitizer doesn't change
these conversions.

## How it was shown

All on GMPE01 Rev 1 at HEAD 87bedfde (N114 in), `--turbo`, one user dir per run.

1. **N106's cases on the current code.** The sanitizer build (`build/n106-fco`, the sweep's list lines):
   - `--match 2@1500` and `5@1500`, party.script: m444 ends after 3746 / 3817 frames. The normal build
     (`normal.sh`, linux-default-relwithdebinfo) gives the same 3746 / 3817 frames.
   - m444 alone, 4 random-pad humans (wander.script): ends after 5016 frames (twice). 4 COMs: 3695 (twice).
   - w02 all COM (`FCO_OVL=90`, board.script): enters m444 at 111773 as in N106, ends after 3794 frames, and the
     party plays to its end (`Start New OVL 1`) within 150000 frames.
2. **Putting the old gravity back.** A scratch build (`build/n117-grav`, the sanitizer build's flags, `grav.patch`:
   `PB_N117_GRAV=1` adds the second 0.3) reproduces N106's numbers exactly with the env var set, and gives the
   current ones without it (`grav.sh`):

   | run | gravity 0.3 (now) | gravity 0.6 (old, some ASLR layouts) | N106 |
   | --- | --- | --- | --- |
   | m444, 4 COMs | 3695 | 3795 | 3795 (run 1), 3695 (run 2) |
   | m444, 4 random-pad humans | 5016 | 11996 (all of it) | 11996 |
   | w02 all COM, m444 at 111773 | 3794 | 38228 (to the end) | 38228 |

   N106's own two COM runs differing (3795 / 3695) is the ASLR split itself.
3. **Where it sits.** gdb on the 0.6 human run (`ball.gdb`, prints the ball every 500 frames): from about frame 4000
   to the end the ball is at x 128, y -98..-102 (the plunger's spot), velocity ±0.3..2, the "stuck" counter
   `lbl_1_bss_312` at 9.
4. **Can a weak human pull hang the real game?** No. `minlaunch.gdb` replaces every launch speed with the weakest
   possible one (`-15`, a pull under 4) on 4 COMs. At 0.3 all three balls clear the lane and m444 ends after 3954
   frames. At 0.6 the first ball falls back onto the plunger and stays for the rest of the 12000 frames. N114's
   "tried first" note (a stick-only launch after a pull of a frame or two bounced for ever) was most likely a
   0.6-gravity run too: at 0.3 the weakest launch gets through.

## Tried and dropped / incidents

- My first batch (11 games at once) froze the user's PC, and the user killed it. The rule is now at most 2 game
  instances at once (AGENTS.md on main). Everything after that ran one pair at a time under `nice -n 19`.
  Earlier, an outside window close (every game's `game_main` returned at 13:59:03, `OSResetSystem is a stub`)
  ended a first batch at about frame 60000. The match results in point 1 come from that batch: both had already
  left m444.
- gdb 18.1 asserts (thread.c set_thread_options, THREAD_EXITED) if it is still attached when partyboard exits. The
  journal's gdb scripts stop at their last needed frame, then `kill` and `quit`. minlaunch.gdb's prints stop at 3500
  because m444's symbols go away when it unloads (an error there also ended gdb). Rerun with the final script:
  3 forced launches, clean exit.
- The scratch edit to pinball.c was reverted right after the scratch build compiled it (diffed against the saved
  copy). Nothing of it is in the commit.

## Not verified

- **The `--match 2/5` parties to match over** on the current code: both runs left m444 but were stopped at about
  60000 frames (the window close, then the user's kill). N114 has w03 to match over at 111988 and w06 at 100528 on
  the same code.
- **Browser build**: not run. The wasm build's 32-bit pointers made the old check read different bytes (N116). The
  fix compiles the check out on every PC target, so web should match desktop now; not run (time, and the 2-instance
  limit).
- **GC's gravity** is still only from N114's reading of the disassembly (0.3, the check never fires). If GC really
  used 0.6, weak launches on GC would hang the same way, but no emulator or hardware was run.
- **Two real peers online** through m444: not run (not needed for the question; N114's fix takes the address out).
- The sanitizer reported musyx `snd3d.c:637/638` float->u8 conversions (1.6e24, NaN) in the w02 and w03 runs. That's
  audio code that N106's sweep didn't report and it's outside this task: filed as **N125**, not investigated.

## Files

- `normal.sh <name> <script> <board>@<frame>`: one `--match` run on the normal desktop build.
- `grav.sh <name> <ovl> <human mask> <script> [1]`: a boot-jump run on `build/n117-grav`, `1` = old gravity.
- `grav.patch`: the scratch change behind `build/n117-grav` (not committed to the source).
- `ball.gdb`, `minlaunch.gdb`: the gdb scripts above (`-ex 'set $minv = -15.0'` for minlaunch).
