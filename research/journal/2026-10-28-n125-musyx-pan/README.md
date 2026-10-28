# N125: musyx snd3d.c:637/638 out-of-range float -> u8 (3D emitter pan)

N117's sanitizer runs reported `clip127((1.f + xPan) * 64.f)` / `clip127((1.f - zPan) * 64.f)` in
`extern/musyx/src/musyx/runtime/snd3d.c` `SetFXParameters` converting 1.6e24, NaN and -7e25 to u8.

## Does it reach netplay state? No

- `SetFXParameters` only sets synth voice controllers (pan 10, surround pan 131) and runs on the MusyX audio thread
  (`audioThread` -> `sndPCRender` -> `snd_handle_irq` -> `s3dHandle`, from the report's stack). It moves with the
  device's 5 ms cycles, not with game frames.
- The game reads nothing back from it during netplay: N112's `src/port/msm.c` gives every status/handle query a
  stubbed answer while `netplay_active()`. The hash log covers no MusyX state.
- Checked: the w02 all-COM run (below) logs the same `--hash-log` before and after the fix, all 150000 frames
  (`cmp` identical).

## Where the values come from

The sanitizer build (`tools/netplay/float_cast_check.sh`, `build/n106-fco`) with `FCO_OVL=90` (w02 all COM,
N106's board.script) reports `snd3d.c:637` (inf) from `s3dHandle:1471` in about 2 minutes. `pan.gdb` (breakpoint on
`__ubsan_handle_float_cast_overflow`) stopped there, right after `resultDll` was linked (overlay change):

- the emitter is sane: flags 0x10009, pos (225, 950, 430), maxDis 3000, head of the emitter list;
- `s3dListenerRoot` is NULL: msm removed its listener (`msmse.c` `sndRemoveListener`) for the overlay change;
- `s3dHandle`'s locals: vol 0, xPan -1.77e32, yPan 4.6e-41, zPan -8.15e33.

`CalcEmitter` loops over the listeners; with none, `n` is 0, `*vol` is 0 and `if (n != 0) { *xPan = ... }` has no
else, so the pans stay whatever the caller's uninitialized locals held. Flag 4 isn't set, so a 0 volume doesn't
key the voice off and `SetFXParameters` runs with garbage pans. The GC's code does the same (the decomp matches it);
the voice is at volume 0 then, so the pan is never heard, and the next update with a listener overwrites it.

So the cast doesn't matter for lockstep, nor audibly. Fixed anyway because it is undefined, x86 and wasm turn it into
different pans, and every later sanitizer sweep would report it again.

## Fix

`cmake/patches/musyx/0001-snd3d-no-listener-pan.patch`: in `CalcEmitter`, with no listener the pans are 0
(centered). Applied at configure by the new `cmake/musyx_patches.cmake` (a copy of `aurora_patches.cmake`'s loop for
extern/musyx, included before `add_subdirectory(extern/musyx)`): extern/musyx is AxioDL's repo, which we can't push
to, so as for aurora the submodule stays at upstream's commit and `git status` shows `m extern/musyx` after a
configure.

Clamping inside `clip127` (through s32) was the other option the brief named; it would hide the symptom and leave
other uses of the garbage (`AddStartingEmitter`'s copies, `sndGet3DParameters`' `info->span`, line 1541) alone.

## Checks

- `FCO_OVL=90` (w02) and `FCO_OVL=91` (w03) all COM, 150000 frames each, on the sanitizer build with the patch: "no
  out-of-range float conversions" (before: snd3d.c:637 on w02).
- w02 hash log identical before/after (above).
- `tools/check.sh` (via `agent-runner done`).

## Not verified

- The browser/Android/iOS/Windows builds with the patch: not built here; the patch is plain C in musyx and the
  configure step is the same CMake for every target, but no non-Linux configure was run.
- By ear: not listened to (the voice is at volume 0 in that state, nothing to hear either way).
- The NaN/-7e25 values N117 saw on w03 weren't reproduced before the fix (only the w02 inf was); they come from the
  same uninitialized locals, and w03 reports nothing after it.
- `tools/check.sh`'s `git submodule update` when a merge moves extern/musyx with the patch applied in its working
  tree: same situation as aurora's patches, not tried.

## Files

- `pan.gdb`: stop at the first float-cast-overflow report and print the emitter, `s3dHandle`'s locals and the
  listener list. Run: `PB_FCO_OVL=90 PB_FCO_HUMAN=0 XDG_DATA_HOME=<dir>/xdg HOME=<dir> gdb -batch -x pan.gdb --args
  build/n106-fco/partyboard --script research/journal/2026-10-28-n106-float-casts/board.script --turbo --hash-log
  <dir>/hash.log` (config.json with `backend.skipBootSequence`/`skipPreLaunchUI` as float_cast_check.sh writes it;
  `WAYLAND_DISPLAY` must be set or SDL finds no video device).
