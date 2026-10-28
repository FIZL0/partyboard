# N120: --turbo runs no longer saturate the desktop GPU

`--script --turbo` ran the sim uncapped (no frame limiter, no vsync), so it also rendered and presented every
tick: hundreds of frames a second per instance. Now the sim stays uncapped, and only about 30 frames a second of
real time are drawn and presented. The other frames are recorded and their GPU work is dropped.

## What changed

- `cmake/patches/aurora/0011-turbo-present-skip.patch`: `aurora_set_present_skip(bool)`. While it is set,
  `gfx::begin_frame` marks the frame packet `skipDraws`. On the render worker, `encode_op` then copies only the frame's
  **texture uploads** and runs its palette conversions. It skips the vertex/uniform/index/storage staging copies,
  the render passes (GX and RmlUi, which goes through gfx passes) and the EFB copies. `gfx::end_frame` swaps the
  caller's callback for one that submits just those copies, with no present, no ImGui, no capture encode.
  The texture uploads have to stay: a texture loaded on a skipped frame is drawn on later ones (uploads go through
  the frame's encoder, `UseTextureBuffer`). The first draft dropped the encoder unsubmitted, like 0010's background
  drop, and would have lost them.
- `frame_present_throttle(bool)` (src/port/imgui.cpp, next to `frame_limiter`): when on, a frame is shown at most
  every 33 ms (steady_clock) and the rest are skipped. main.c calls it once per tick with `netplay_debug_turbo()`,
  just before `aurora_begin_frame_always`. When it is false it does nothing (offline and lockstep are unchanged).
  A `PB_ICON_SHOT` run keeps every frame: a capture due on a skipped frame would be lost.
- `tools/netplay/gpu_busy.sh <cmd>`: samples every amdgpu card's `gpu_busy_percent` once a second while the command
  runs, then prints the mean and max.

The skip decision depends on wall-clock time. That is fine because nothing in the game reads GPU results: no
GXPeekZ/GXPeekARGB in src/, and depth_peek has no game caller. The hash logs show it.

## Measurements (tools/netplay/gpu_busy.sh tools/netplay/party_check.sh, two parallel turbo parties)

The GPU is card1. card0 stayed at 0% throughout. Its idle load from the desktop and other agents' work moved
between 8% and 18% during the session, so the numbers carry that much noise.

| build | card1 mean | max | wall time | hash logs |
|---|---|---|---|---|
| before (main) | 78.6% | 93% | 180 s | identical, 135000 frames, match over at 110988 |
| skip, staging copies kept | 25.9% | 42% | 151 s | identical |
| skip, shown every 1 s (experiment) | 25.3% | 41% | 149 s | identical |
| **final** (texture uploads only) | **22.9%** | **26%** | 152 s | identical |
| skipped frames submit nothing (experiment, idle was 17–18%) | 20.7% | 41% | 160 s | identical |

The final run's hash log also matches the pre-change build frame for frame (hashdiff before/run1 vs after2/run1:
identical, 135000 frames). Showing frames 1 a second instead of 30 changed nothing. Removing the last upload
submits changed about 2 points, within the idle noise. So what is left is mostly other load on the shared GPU, not
these runs. As a side effect the runs are about 15% faster.

Visual check: I ran a turbo party with the PB_ICON_SHOT guard switched off by hand (an experiment, reverted
afterwards) and captures every frame 1..400 after each icon. 388 PNGs were written: every capture that fell on a
shown frame. The board (ovl 89) and file select (ovl 74) frames render fully textured
(`turbo-shown-frame.png`), so no uploads are lost.

## Things that failed

- 0011 first edited `aurora.cpp` begin/end_frame and `frame_capture.*` (to keep a frame shown when a capture is due).
  That broke the configure. `aurora_patches.cmake` skips a patch whose **reverse** check passes, and editing inside
  0003's and 0010's hunks or context makes their reverse checks fail, so the configure tries to apply them again
  and stops. Changing 0003 itself would break every tree that already has it applied. So 0011 touches only
  `lib/gfx/common.cpp`, plus one declaration in `aurora.h` away from 0010's hunk. I checked that all patches
  reverse-check on the patched tree and that the whole series applies forward on a clean `git archive` of the
  submodule's HEAD.

## Not verified

- Browser, Android and iOS builds: not built. The new code is shared C++ with no platform branch (the throttle is
  only reached with `--turbo`, which is desktop scripting), but `common.cpp` under `__EMSCRIPTEN__` was not
  compiled.
- The skipped frames' EFB copies and offscreen passes are not encoded. So a shown frame whose effect samples an EFB
  copy that was made on an earlier, skipped frame shows a stale copy. That is visual only, and only under
  --turbo. I didn't find such a frame in the captures, but I didn't look for one on purpose.
- A real GPU-lag check on the user's desktop (mouse lag) wasn't possible unattended; the evidence is
  gpu_busy_percent only.
