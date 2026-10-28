# N114: m444 never ended in party.script (w03, turn 6)

m444 (ovl 52) is the board event's pinball: one player launches three balls in turn, each with a plunger. Two
things kept `tools/netplay/party.script` in it for ever, and one of them is a real netplay bug.

## 1. The plunger needs a stick pull (the original hang)

`fn_1_9418` (pinball.c) waits for the player's pull and A:

    if ((HuPadBtnDown[pad] & PAD_BUTTON_A) && lbl_1_bss_788[arg0] > 0) break;

`lbl_1_bss_788` (the pull) only grows while the stick is down (`-(stkY >> 3) / 20` a frame, up to 50); with the
stick centred it shrinks to 0. party.script only taps A, so a human player never launches. With the old script, gdb at
frame 60000 (breakpoint on HuPrcVSleep, `bt`, enabled from `hash_log_line` at 60000) shows the main process in
`fn_1_9418` (pinball.c:336). The state hash cycles because it doesn't cover m444's own statics; the cycle is the pad
section and board/character animation.

**Tried first, not enough:** a generic `ovl-stick <ovl> <port> <x> <y>` script line (stick held down while ovl 52
runs). The ball then launches, but on whichever A tap comes first, after a pull of a frame or two: launch speed
`15 + pull / 4` (its COM pulls 20-119 frames). That ball rose to y -100, bounced off the lane's top and fell back to
the plunger, for ever (logged: x 128, y -200..-100, same speeds each cycle). Each launch comes a fixed delay after
the A that closed the previous window, so no tap cadence guarantees a full pull for all three launches. Dropped.

**Fix (script side, as N79's lottery-stick):** the plunger loop calls `netplay_script_plunger_wait(pad)` every frame
it waits (`#ifdef TARGET_PC`, `if (netplay_active())`, 3 lines in pinball.c). With the script line
`plunger-stick on`, `script_pads` gives that pad stick Y -120 and drops its A until it has waited 90 frames
(`PLUNGER_PULL_FRAMES`, a pull of ~40 of 50, mid-range for the COM); the next scripted A tap launches. Outside a
script (`script.plunger_stick` false) the call does nothing, and offline it isn't made at all. party.py writes the
line; party.script regenerated.

## 2. The ball's gravity depended on ASLR (netplay desync)

With the plunger fixed, two w03 runs with the same pads split at frame 58085 inside m444 (frand), and some w06 runs
still never left m444. In 6 runs: 4 had one outcome, 2 the other; ASLR off (setarch -R) split too, ASLR on matched
once: random per run. Debug prints (removed): the collision lines from `fn_1_D1E0` hash the same in every run, but
the ball's velocity changed by +0.6 a frame in some runs and +0.3 in others from the 2nd frame of rolling.

The cause, in `fn_1_B1E8` (VERSION=1 is ENG1, so `VERSION_REV2` code is in the build):

    arg1->y += 0.3;
    #if VERSION_REV2
    if (VECMag((Vec *)&arg1) < 0.000001) { arg1->y += 0.3; }

`&arg1` is the address of the pointer, so this is the length of 12 stack bytes. GC's code (m444dll.rel from the Rev 1
disc, N69's ciso_extract.py + ppcdis.py, fn_1_B1E8 at .text 0xB1E8): `stw r4, 8(r1)` spills arg1; 12(r1) is the
volatile float of the inline `sqrtf(6.0f)` (`stfs f0, 12(r1); lfs f26, 12(r1)`, computed on every call for the
stuck test), 16(r1) is `spC` (int 1). So on GC the vector is (0x80xxxxxx as float, under 1e-37; 2.449; 1.4e-45), its
length 2.449, and the extra 0.3 never happens. On x86-64 the 12 bytes are the 64-bit pointer's halves and 4 bytes of
whatever: the low half's exponent bits come from the address, so with ASLR some runs got gravity 0.6.

**Fix:** `#if VERSION_REV2 && !defined(TARGET_PC)` around that check, with the GC reasoning in a comment: PC does
what GC does (0.3), in every run. Online, two peers could split here before this (each its own ASLR).

The same `(Vec *)&<pointer>` pattern is in m402/m405/m432 `fn_1_1FC4` (`VECAdd((Vec *)&arg0, &sp18, (Vec *)&arg0)`,
a 12-byte write over an 8-byte pointer at the end of the function), m423 `fn_1_137C4` (`(Vec *)&arg3`, five
times) and m421 player.c:1054 (`VECMag((Vec *)&sp8.y)`, reads 4 bytes past sp8). Not touched here: filed as **N118**.

## Checks

All on the desktop build (linux-default-relwithdebinfo), GMPE01 Rev 1, `--turbo`, party.script as committed, one
user dir per run:

- w03 (`--match 2@1500`), two runs at once: both reach `match over at frame 111988`, hash logs identical for all
  135000 frames (hashdiff.py). m444 runs twice in that party (from 56858 and from 97130, a later board event), 7677
  frames in all, and ends both times.
- 10 runs at once (4× w03, 4× w06, 2× w01; the machine was so loaded that the 1500 s kill came before w03/w01's end):
  all four w03 logs identical as far as they got (107262+ frames, past both m444s); all four w06 runs (`5@1500`)
  reach match over (100528, 100528, 100528 and 97088), m444 3817 frames in each. w06 run 2 split from the others at
  65201 in ovl 29 (m421, frand), the m421 cast filed as N118. Before the gravity fix 2 of 6 w03 runs split in m444.
- w01 (`0@1500`, the party_check.sh board, no m444): `match over at frame 110988`.
- Old script, same build: still hangs in `fn_1_9418` (gdb), as expected.
- `tools/check.sh`: passes.

Related tasks: N117 (m444 sat to the end in N106's sanitizer sweep, all-COM m444 ended, random-pad humans didn't) is
most likely part 1 (a human seat at the plunger), and its party runs part 2 as well; not rechecked in N106's setup.
N116 (w06: desktop to m444 at 49656 and hung, the browser played on) had m444 in it too: the wasm build's pointers
are 32-bit, so its `VECMag((Vec *)&arg1)` read different bytes than the desktop's.

## Not verified

- **Browser build**: not built or run (tools/web.sh; time). The change is plain C in netplay.c and pinball.c; the
  wasm build's m444 should now match the desktop's, unchecked.
- **Two real peers online** through m444: not run; the ASLR split was shown with two local runs of one build, and
  the fix removes the address from the computation.
- **party_check.sh** itself (two w01 runs + hashdiff) not run as a script; one w01 run reached match over, and the
  w01 party never enters m444.
- **GC's value** of the check comes from reading the disassembly, not from running GC (no emulator here); if the
  12 bytes were different on hardware, gravity would be 0.6 and only the feel of m444 would change, still the same
  in every run.
- The m421 split in w06 run 2 is not investigated (N118).

## Harness

- Runs: `build/n114/run.sh <name> <script> <board>@1500` (one user dir per run, `timeout -s KILL 1500`,
  `--turbo --hash-log`), many in parallel.
- Where a hung run waits: gdb from the start (can't attach, ptrace_scope 1), `break hash_log_line if at == 60000`
  whose commands enable `break HuPrcVSleep` with `bt 5`, quit after 60 hits; count the `#1` frames. gdb turns ASLR
  off, which changed m444's outcome here (part 2), so compare outcomes without gdb.
