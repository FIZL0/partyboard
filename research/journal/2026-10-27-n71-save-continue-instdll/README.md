# N71: save-continue.py presses START by overlay, not by frame window

## What changed

`tools/netplay/save-continue.py` had fixed START windows (save 6300-6510, continue 5340-5550). instDll moved to
frame 7465 on the save run (N42 measured 7545 on its base), so the save run tapped START before instDll existed and
then sat in instDll until `end 14000`. The script now emits N53's `ovl 3 0 0 START` and taps only A. While instDll
(ovl 3) runs, the script turns every port-0 press into START alone. The frame tables are gone, and the docstring
has the new frames.

## Tried and failed

- `ovl 3 1 0 START` (the brief's guess, and what `omOvlCallEx(DLL_instdll, 1, 0, 0)` seems to say). The second
  argument of `omOvlCallEx` goes to `omOvlKill`. The event is the third argument, and it is 0
  (`objmain.c:94` logs `Start New OVL 3 (EVT:0 ...)`). With event 1 the save run stayed in instDll from 7465 to 14000.

## Checks (GMPE01 Rev 1, `$PB_DISC`, fresh user dirs under build/n71run, `--turbo`, `--match 0@900`)

| run | result |
|---|---|
| `save` | instDll 3@7465, minigame 17@7748, resultDll 84@13571. Checkpoints 13741 (turn 1/10) and 13814 (turn 2/10), the same 13814 N42 got with its hand-tuned 7615-7825 window. |
| `title --continue <save json>` | instDll 4696, 26@4987, resultDll 9035, checkpoints 9241 / 9313 (turn 3/10), second instDll 14763 -> 51@15047. Reached turn 4/10 by 20000. |
| `mgmode --continue <save json>` | same overlays and checkpoints as `title` |
| `hashdiff.py --from 900 title.log mgmode.log` | identical: 19100 frames |

The continue runs pass two instDlls each, so the rule also works on an instDll that starts after the first one.

`tools/check.sh` passes (it does not run the disc scripts).

## Not verified

- Other disc revisions: only GMPE01 Rev 1 is on this machine.
- Two-peer (network) save/continue runs with these scripts. The scripts drive single-process `--script --match`
  runs only, and N42 already covers two peers with its own presser.
