# N116: browser vs desktop splits on w02 and w06 (party.script --turbo)

Brief (from N107): under `tools/netplay/party.script --match B@1500 --turbo` the browser and the desktop agreed on
w02 up to the reseed at 16899 and on w06 up to 48229, then went apart. Get both hash logs, hashdiff for the first
frame, run float_cast_check.sh on those boards. N107 guessed N106's float casts.

## Runs (this branch's base, b25ea1a9: N106, N114, N120 in)

- Desktop: `desk.sh NAME BOARD` (here): the release desktop build, party.script, own user dir. Two runs per board
  had identical hash logs (no run-to-run noise).
- Browser: `web.sh NAME BOARD PORT SECS` (here; `ROOT=` picks a copy of build/web): `tools/web_check.mjs`, headless
  Chromium on the GPU, GMPE01 Rev 1. It plays `party-noend.script`, party.script with `end 999999` instead of
  `end 135000` (`sed 's/^end 135000$/end 999999/' tools/netplay/party.script > build/n116/party-noend.script`): the page then never reaches the script end (where it stops answering CDP, N107) and the hash log is
  pulled while it runs with N107's `cdp_file.mjs` (512 KB chunks). The pad lines are the same; `end` only stops
  the run.
- `tools/netplay/hashdiff.py desktop.log browser.log` on each pair.

## w02: the roulette's out-of-bounds read

hashdiff: first differing frame **18216** (overlay 90 = w02Dll, turn 2, player 2): `hash`, `sys`, `plr0-3` equal,
only `frand` differs. The player state follows at 18220.

`frandlog.diff` (here, not committed): `PB_N116_FR=<from>-<to>` logs every `frand`/`frandmod` caller in those
frames (desktop: `backtrace` + `dladdr`; browser: `emscripten_get_callstack(EM_LOG_JS_STACK)`, which needs a
`WEB_FAST=1` build to keep the names, and the page query `env=PB_N116_FR=...`). Desktop: no call in 18150-18260.
Browser: ~80 `frand` calls at 18216, all from `w02Dll.fn_1_D3AC <- fn_1_CD04 <- fn_1_BCFC` (roulette.c): the
Goomba roulette's coin shower, which `fn_1_CD04` runs only for a negative result of `fn_1_C108`.

`fn_1_C108` (the ball) ends with

```c
var_f31 = atan2d(-sp1C.z, sp1C.x);   // -180..180
var_f31 = var_f31 - sp34.y;          // the wheel's rotation, 0..360
if (var_f31 < 0.0f) var_f31 += 360.0f;   // still negative when the wheel has passed the ball
var_r30 = (s32)(var_f31 / 22.5f);    // so -8..15
...
return lbl_1_data_4D0[var_r30];
```

A logged desktop run (`diaglog.diff`, `desktop-c108.txt`): the roulette's second spin (turn 2) has the wheel at
348.75 and the ball's angle ends at -23.4, so `var_r30 = -1` and the game reads the word before the 16-entry
table. The desktop's ELF had some value >= 0 there, the browser's wasm data segment a negative one, so only the
browser rained coins. Not a float problem: the floats agreed, the index was out of range on both.

**What GC does:** roulette.c is a matching object; in the Rev 1/Rev 0 REL (`config/GMPE01_00/rels/w02Dll/
splits.txt`, `symbols.txt`) roulette's `.data` starts at 0x4D0 right after shuffleboard's, which ends with
`lbl_1_data_4B0[8]` (0x4B0-0x4CF). So index -8..-1 reads `lbl_1_data_4B0[0..7]` = `DATA_MAKE_NUM(DATADIR_W02,
0x26..0x2D)`, always a large positive number; `fn_1_CD04` then finds no link with that number and puts the player on
the space link it checked last. No coin shower on GC.

**Fix** (src/REL/w02Dll/roulette.c, `TARGET_PC`): a negative `var_r30` returns `lbl_1_data_4B0[var_r30 + 8]`,
GC's word. Offline play changes too (it read linker garbage before); that is the GC behaviour, as N114 did for m444.

## w06: m421's stack read (N121's)

hashdiff: first differing frame **65201** (overlay 29 = m421Dll, turn 7), again `frand` first. Since N114 fixed
m444 both builds play through m444 (49656-53473) identically; N107's split at m444 was N114's ASLR gravity.

m421 is the site N114 already listed for **N121**: player.c fn_1_7AA0 `VECMag((Vec *)&sp8.y)` reads sp8.y, sp8.z
and the 4 bytes after sp8, which are different stack contents on every platform. On GC the decomp's names are
stack offsets: sp8 at r1+8..0x13, sp14 at r1+0x14, so GC reads `sp14.x` (= `work->unk_D8.x * 7.5`, unchanged by
the loop). N121 owns that line (filed for m402/m405/m432/m423/m421 together), so it is **not committed here**;
`m421-test.diff` (here) is the GC-equivalent `{sp8.y, sp8.z, sp14.x}` used only for the check below.

## float_cast_check.sh

`FCO_MATCH=1@1500` and `FCO_MATCH=5@1500` with party.script: **no out-of-range float conversions**, 135000 frames
each (built before the fixes; the paths up to the splits are the ones above). N106's sweep had already found none;
neither split is a float cast.

## After the fixes

Both builds with the roulette fix **plus `m421-test.diff`** (browser: `WEB_FAST=1` build), party.script, 135000
frames:

| board | desktop vs browser | match over (ovl 78) | frame 135000 hash |
|---|---|---|---|
| w02 (`--match 1`) | **identical for all 135000 frames** (heap= too) | 118587 (was 120027 desktop / 112467 browser) | `700AA426BF67E95F` both |
| w06 (`--match 5`) | **identical for all 135000 frames** (heap= too) | 97087 (the browser's old value; desktop was 100527) | `28783F3957B7BEAC` both |

Two desktop runs per board, before and after the fix, were identical to each other. A first batch of four
fixed desktop runs all exited with status 0 in the same second at different frames (no core, mid-party, in m432
and on w06's board): something outside closed the windows; rerun to full length (`g02`, `g06` above).

**What is committed** (the roulette fix only, no m421 change): the desktop w02 log is identical to the patched
one for all 135000 frames: with the roulette giving GC's result the party never reaches m421 (overlays 28, 62,
27, 32, 13, 36, 34, 25, 40, 42). Release browser build (full `tools/web.sh`, wasm-opt -O2): **identical to the desktop for all 135000 frames** (same final hash `700AA426BF67E95F`).

w06 still splits at 65201 in m421 on the committed code until N121 makes `fn_1_7AA0` read what GC reads; with
`m421-test.diff` it agrees for the whole party, so N121 has a tested candidate for that one site.

## Not verified

- **w06 with the committed code alone**: not rerun; it is the unfixed m421 read (the first split, 65201, is in
  m421 on the base build, and with only `m421-test.diff` added the boards agree). Left to N121, which owns that
  line and four similar ones (m402/m405/m432 fn_1_1FC4, m423 fn_1_137C4), not checked here.
- **GC's value in m421**: inferred from the decomp's stack-offset names (sp8 at r1+8, sp14 at r1+0x14), not from
  the disc's code (N69 ppcdis.py); N121's brief asks for that reading.
- **GC's roulette**: the word before `lbl_1_data_4D0` comes from the GMPE01_00 splits/symbols (roulette's .data
  starts where shuffleboard's ends); not read from the Rev 1 REL itself or run on GC/Dolphin.
- **The desktop's old index -1 value**: not looked up (some value >= 0; it no longer matters).
- **Other boards** (w01, w03, w04, w05): not rerun after the fix (only w02 has this roulette). N107 had w03/w04
  agreeing; w05's split (29641/30114, N107) is not this task's.
- **Online two-peer (desktop + browser) match**: not run; the check is the same script on both, hash logs compared.
- **Firefox/WebKit, Android, iOS**: not run. The out-of-bounds read differs with each platform's data layout, so
  the fix applies to all; not checked there.
