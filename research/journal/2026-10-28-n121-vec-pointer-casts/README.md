# N121: decomp casts that read a pointer or the stack as a Vec

N114 found that m444's `VECMag((Vec *)&arg1)` read the stack instead of a vector. This task covers the other sites of
the same kind. `grep -rnE "\((Vec|HuVecF) \*\)&" src` finds them all. Most hits cast a `Vec` to `Vec *` (harmless).
The ones that aren't:

| site | what it does | GC (disc code) | x86-64 before | gameplay |
|---|---|---|---|---|
| m421 player.c `fn_1_7AA0` | `VECMag((Vec *)&sp8.y)` | length of (sp8.y, sp8.z, **sp14.x**) | 3rd float = low half of the **stack canary** | **yes: COM weights split runs and peers** |
| m423 main.c `fn_1_137C4` (6 cases; the brief said 5) | `VECAdd((Vec *)&arg3, &sp10, (Vec *)&arg3)` then `return 1` | writes its own spill slots, no effect | write lands in frame padding, no effect | none (verified hash-identical) |
| `fn_1_1FC4` in m402, m405, m429, m432, m441, m450, m461 (the brief listed 3) | `VECAdd((Vec *)&arg0, &sp18, (Vec *)&arg0)` at the end | writes its own spill slots, no effect | UB write past an 8-byte slot | none: **the function has no callers** |

## How GC's code was read

I used N69's `ciso_extract.py` on `PB_DISC` (GMPE01 Rev 1) and its `ppcdis.py rel <rel> <off> <size>`. The offsets
come from `config/GMPE01_00/rels/*/symbols.txt`: Rev 1 (`config/GMPE01_01`) only has its own symbols for m444 and
mstory2, so the other RELs are the same as Rev 0. Extracted files stayed in the scratchpad.

- **m421 `fn_1_7AA0`** (.text 0x7AA0, 0x418): VECScale writes sp14 at `addi r4, r1, 20`. VECSubtract writes sp8 at
  `addi r5, r1, 8`. Then `stfs f0, 12(r1)` (sp8.y = 0) and `addi r3, r1, 12; bl VECMag`. So GC takes the length of
  12(r1)..23(r1) = sp8.y (0), sp8.z, sp14.x (= `work->unk_D8.x * 7.5`, which the loop doesn't change). N116 had
  inferred this from the decomp's offset names; the disassembly confirms it.
- **m423 `fn_1_137C4`** (0x137C4, 0x78C): `stw r6, 8(r1)` spills arg3 and `stw r7, 12(r1)` spills arg5. sp10 is at
  `addi r4, r1, 16` (VECScale's output). Each case ends with `addi r3, r1, 8; addi r5, r1, 8; bl VECAdd` and returns
  1. The 12 bytes written are arg3's slot, arg5's slot and sp10.x, and none of them is read after that. `*arg3`
  never gets sp10 added.
- **`fn_1_1FC4`** (0x1FC4, 0x3F0): the disassembly is byte-identical in all 7 RELs. `stw r3, 8(r1)`,
  `stw r5, 12(r1)`, `stw r6, 16(r1)` (arg0, arg2, arg3). The final `addi r3, r1, 8; addi r4, r1, 24;
  addi r5, r1, 8; bl VECAdd` writes those three slots, and the epilogue restores the saved registers from 56(r1) up,
  so nothing changes. No source file calls it or takes its address (`grep fn_1_1FC4` finds only the declarations and
  definitions). The decomp matches, so the RELs have no call to it either.

## What PC did (x86-64 desktop build, `objdump -d -M intel`)

- **m421:** sp8 is at `[rsp+0x4c]` and VECMag gets `[rsp+0x50]`. Its third float is `[rsp+0x58]`, where the
  prologue stores `fs:0x28`, the stack-protector canary. That value is random per process. So two runs of the same
  build split here: not ASLR, the canary. The browser (wasm, no canary) reads yet another value. That explains N114's
  "w06 run 2 split at 65201 in ovl 29" and N116's w06 desktop/browser split.
- **m423:** arg3 is spilled at `[rsp+0x8]`. The write covers `[rsp+0x8..0x13]`, and 0x10..0x13 is unused padding in
  this build. So there is no visible effect, only undefined behaviour that another compiler or ABI (wasm's 4-byte
  pointers make the write 8 bytes too long, arm64) may lay out differently.
- **fn_1_1FC4:** never runs.

## Fix (all `TARGET_PC`, GC code unchanged under `#else`/`#ifndef`)

- m421: `Vec gcVec = { sp8.y, sp8.z, sp14.x }; var_f28 = VECMag(&gcVec);` (the same values as N116's
  `m421-test.diff`).
- m423: the six `VECAdd((Vec *)&arg3, ...)` are left out on PC, with a comment above the function. GC drops their
  result too.
- fn_1_1FC4 ×7: the final VECAdd is left out on PC.

## Checks

- **m421 alone, all COM** (N106's sanitizer build `build/n106-fco`, `FCO_OVL=29`, `com.script`, 12000 frames, two
  runs at once): **before**, the two runs split at frame 799 (hash log line 800). **After**, they are identical for
  all 12000 frames. No float-cast reports either way.
- **m423 alone** (`FCO_OVL=31`), all COM (`com.script`) and 4 humans (`wander.script`, `FCO_HUMAN=15`), 12000
  frames: the hash logs before and after the change are **byte-identical** (expected: neither GC nor PC ever let the
  write reach `*arg3`). The minigame ran and returned to ovl 1 in all four runs.
- **w06 party** (desktop `relwithdebinfo`, `party.script --match 5@1500 --turbo`, `desk.sh` here, two runs at once):
  both reach `match over at frame 97088`, and the two hash logs are identical for all 135000 frames. They are also
  **byte-identical to N116's `g06` log** (roulette fix + `m421-test.diff`), which N116 showed the release browser
  build matches for all 135000 frames. m421 (ovl 29) is in that party. None of the other changed overlays are.
- `tools/check.sh` (run by `agent-runner done`).

## Not verified

- **Whether `fn_1_137C4`'s `return 1` paths ran** in the m423 runs: there was no hit count, so "identical before and
  after" may only say the paths weren't reached. Either way the analysis above says the change can't alter state on
  x86-64.
- **m402/m405/m429/m432/m441/m450/m461 runs**: not played, because the changed function is dead code and only the
  build covers it.
- **Browser on this commit**: not rebuilt. The m421 value equals N116's tested `m421-test.diff`, and the w06 desktop
  log equals the one N116's browser run matched.
- **Two real peers in an online match, Android/iOS (arm64)**: not run. The fix removes a read that depended on the
  platform and process, so they now compute the same thing, but no device was run.
- **On GC hardware / Dolphin**: the GC values come from the disassembly, not from a GC run.
