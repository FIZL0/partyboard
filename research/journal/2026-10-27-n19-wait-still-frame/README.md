# N19: present the last frame plus RmlUi during a lockstep WAIT

## Problem

During a lockstep WAIT (N5), `src/game/main.c` skipped `aurora_begin_frame`/`aurora_end_frame`. The window
kept its last image, but RmlUi (toasts, the N10 "Waiting for <name>" overlay) is drawn only inside
`aurora::end_frame` (`rmlui::record_frame`), so it froze too. An empty aurora frame is no fix:
`gfx::begin_frame` opens pass 0 with `clearColor = true` (`extern/aurora/lib/gfx/common.cpp`, the
`RenderPass` default), so the present source, which is the EFB itself (`webgpu::present_source()`; aurora's
`GXCopyDisp` is a no-op), comes out cleared.

## Options weighed

- **Patch aurora** (e.g. `aurora_begin_frame` with a "load, don't clear" flag for pass 0): the cleanest fix, one
  line in `gfx::begin_frame`. But `extern/aurora` is a submodule of upstream `encounter/aurora`, and nobody
  here can push to it. A local submodule commit would leave the superproject pointing at a commit no one
  else can fetch. Calling the internals from port code is not possible either: `g_currentRenderPass`,
  `g_framePackets` and the rest are `static` in `common.cpp`.
- **Keep a copy of the last frame with the public GX API**: this is what I did. The game already does the
  same thing in `src/game/wipe.c`: `WipeCrossFade` copies the EFB with `GXCopyTex` and `WipeFrameStill` draws
  it back as a quad. That code already runs on aurora, so the method is known to work there.

## What was done

- `src/port/netplay_still.c` (new):
  - `netplay_still_keep` copies the whole EFB (`RenderMode->fbWidth` x `efbHeight`, `GX_TF_RGB565`) to a
    host-heap (`calloc`) buffer. aurora keys copy textures by the dest pointer. In aurora, `GX_TF_RGB565` is
    only "alpha = 1" on an RGBA8 texture, so the colour is not reduced to 16 bit. The buffer comes from the
    host heap, not `HuMemDirectMalloc`, so `netplay_heap_hash` and the game heaps do not change.
  - `netplay_still_draw` sets up an ortho quad the way `WipeFrameStill` does, with REPLACE, no blend, no z,
    no fog and no cull. It saves projection, viewport and scissor with `GXGet*` and restores them afterwards.
  - `netplay_still_drop` frees the buffer. It is called from `netplay_end`.
- `src/game/main.c`:
  - The end of each tick runs `netplay_still_keep()` when `netplay_lockstep()` is true and that frame's
    `aurora_begin_frame()` succeeded (`framed`). `GXCopyTex` outside a recorded frame would index pass
    `UINT32_MAX` in `resolve_pass`.
  - A WAIT frame now runs `aurora_begin_frame` → `netplay_still_draw` → `ui_update` → `aurora_end_frame`.
    If begin fails (minimised or paused window), it only calls `ui_update`, which is the old behaviour.
  - Offline and `--script` runs (no lockstep) are unchanged: both hooks sit inside the existing
    `netplay_active()` paths.
- `tools/netplay/tick_gate_test.c`: stub `netplay_still_drop`. That test links `netplay.c` without the GX
  side.

## Costs and caveats

- Every lockstep tick pays for one extra full-EFB resolve, plus one extra render pass after it (a load pass
  with no draws). The copy has to be made every tick, because the next frame's pass 0 has already cleared the
  EFB by the time the game knows it must WAIT.
- If WAIT comes before any lockstep tick has been kept, nothing is drawn. The frame is then the clear colour
  with the UI on top. That is better than a frozen UI.
- If aurora drops its copy-texture cache (`clear_copy_texture_cache`), `GXInitTexObj` falls back to the
  zeroed buffer, and the still frame is black until the next tick.
- A WAIT frame leaves its GX state behind (TEV, channels, vertex format, blend, z mode). The next tick's draw
  code sets these itself. MP4 code is written to expect leftover state from the sprite, 3D, wipe and font
  passes, which all share the GPU. Projection, viewport and scissor are restored anyway.

## Not verified

- **Running it**: there is no GMPE01 disc image in `orig/GMPE01_00/`, so I could not start the game, hold a
  lockstep WAIT, or see the still frame with the overlay. Only the build and the C tests were checked:
  `tools/check.sh`, with `tick_gate_test` and `match_test` both passing. Still to check by eye once a disc
  is there: the image under the overlay matches the last tick, it is not flipped or offset (the quad follows
  `WipeFrameStill`), it scales right with FIT/STRETCH viewport policy and with MSAA, and the first tick after
  a WAIT draws correctly.
- Per-tick GPU cost of the extra copy: not measured (needs the game). Both are filed as N28.
