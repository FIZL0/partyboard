# N118: survive a Dawn device loss

aurora's `SetDeviceLostCallback` FATALed on any loss (driver reset, GPU fault, a phone's GPU process restart). Now the
callback only marks the loss and the next `aurora_update` makes a new device and every GPU object again. The game's
state is untouched: a scripted run with device losses has the same hash log as one without.

## What changed

All in a new aurora patch, `cmake/patches/aurora/0012-device-loss-recovery.patch` (described in
`cmake/aurora_patches.cmake`), plus two test steps in `src/port/lifecycle.cpp`.

- **webgpu (`lib/webgpu/gpu.cpp`)**: the lost callback ignores `Destroyed` / `FailedCreation` / `CallbackCancelled` (our
  own releases, a failed request) and the shutdown; otherwise it logs and sets `g_deviceLost` (atomic: Dawn may call
  it from any thread, inside any call). `shutdown()` is split: `release_device()` drops every device object (copy and
  resample pipelines, frame/depth buffers, queue, surface (unconfigured), device, adapter, adapter info, surface
  capabilities) and clears the flag, but keeps the instance and the Dawn pipeline-cache store; `shutdown()` =
  `g_shuttingDown` + `release_device()` + instance + `cache_shutdown()` as before (so `g_shuttingDown` is still set
  only for good). `initialize()` already reused an existing instance, so it is the recovery's re-init as is.
  `force_device_loss()` = Dawn's `Device::ForceLoss` (not in emdawnwebgpu: logs and does nothing in the browser).
- **gfx (`lib/gfx/common.cpp`)**: `shutdown_for_device_loss()` is `shutdown()` except that gx and texture_replacement
  only release GPU objects. A generation counter, bumped by shutdown, makes a map callback of an old staging buffer
  (`AllowSpontaneous`, it may come late) leave the new buffers' map states and staging slots alone; a non-success map
  on a lost device counts as "unmapped" instead of tripping the ASSERT.
- **gx (`lib/gx/gx.cpp`)**: `release_gpu_objects()` is `shutdown()`'s GPU half: layouts, the texture/TLUT/palette
  caches, the copy-texture caches, and the whole texture binds (`item = {}`, not `ref.reset()`:
  `resolve_sampled_textures` keeps a bind whose texObj id/version match the loaded one, so a reset ref would stay
  null). `loadedTextures` / `loadedTluts` (what the game loaded with GXLoadTexObj/GXLoadTlut) stay: the next draw
  resolves them on the new device.
- **texture_replacement**: `release_gpu_objects()` drops the made textures (cache + LRU), keeps the registrations and
  the pending/loaded TLUT metadata (game state).
- **imgui (`lib/imgui.cpp`)**: `release_device_objects()` / `create_device_objects()` = `ImGui_ImplWGPU_Shutdown` / `Init`
  with the new device (the context and the SDL3 platform backend stay). Textures from `aurora_imgui_add_texture` die
  with the device (logged); partyboard adds none.
- **RmlUi (`lib/rmlui.cpp`, `WebGPURenderInterface`)**: `Rml::ReleaseTextures()` (every file and callback/font texture
  RmlUi made; RmlUi makes them again when a document is next rendered), the render interface's own null texture, clip
  mask, layers, postprocess and blend-mask targets, and rmlui's render target + bind group; then the null texture again
  for the (maybe new) surface format. Not `Rml::ReleaseRenderManagers` (it only drops managers no context uses, and
  needs the contexts gone) and not `rmlui::shutdown` (it would drop partyboard's documents). Compiled geometry,
  filters and shaders are CPU-side in this render interface: nothing to release.
- **aurora (`lib/aurora.cpp`)**: `recover_device()`: `gfx::shutdown_for_device_loss()` (render worker synced and
  joined, pipeline cache thread stopped) → rmlui/imgui release → `webgpu::release_device()` → `webgpu::initialize(the
  backend in use)` → `gfx::initialize()` (buffers, bind groups, gx layouts, rmlui pipeline layouts, pipeline cache
  reloaded from its store, render worker restarted) → imgui/rmlui create. Called from `aurora_update` (between frames,
  game thread), and only while the window is presentable (iOS kills an app doing GPU work in the background; frames
  can't begin meanwhile). More than 3 losses within 10 s still FATAL ("giving up"), so a device that dies on every
  frame doesn't loop for ever. `aurora_force_device_loss()` (aurora/gfx.h) for tests.
- **`src/port/lifecycle.cpp`**: `PB_LIFECYCLE_TEST` steps `lose` (aurora_force_device_loss) and `shot` (the game frame
  to `$PB_LIFECYCLE_SHOT/loopNNNNN.png` via aurora_capture_frame).

The game side needed nothing: since N110 `main.c` skips the tick when `aurora_begin_frame_always` can't begin a frame
(the lost device's staging map is aborted), so the frame of the loss costs one or more loops but no game frame.

### The patch had to keep clear of the others

`cmake/aurora_patches.cmake` decides "already applied" by `git apply --reverse --check` of each patch on its own.
My first version edited lines inside other patches' hunks (0004 in gpu.cpp/common.cpp, 0007's staging declarations,
0010's `begin_frame` rewrite and common.hpp, 0011-turbo's aurora.h lines), so 0004's reverse check failed and the
configure stopped. Chaining the reverse checks does not work either: git 2.56 doesn't feed one patch's result to the
next in `git apply --reverse --check A B` (nor in one concatenated stream). So the hunks were moved apart: the flag
is cleared in `release_device()` (not after `GetQueue`), the recovery runs in `update()` in the 3-line gap between
0004's hunk and 0010's, `aurora_force_device_loss` is declared in gfx.h, and the declarations/wrappers sit elsewhere.
`harness/pcheck.sh` regenerates 0012 from a snapshot of the patched tree and reverse-checks every patch; the whole
stack was also applied forward on a `git archive` of aurora's HEAD and compared with the working tree (identical).

## Checks

All Linux, Vulkan (RX 9070 XT, radv), `PB_DISC`, own user dirs (`harness/run.sh`), `mash-a.script` cut to `end 2400`,
`--turbo --hash-log`.

1. **Hashes**: `lose@600,lose@1500`: two "GPU device recreated", `hashdiff.py` against a run without:
   `identical: 2400 frames` (and the same final hash `ED02C0D597EE16B7`). Also identical: 3 losses (600/1500/2100)
   with Dawn validation on, and `bg@500,lose@550,fg@700,lose@1200`.
2. **Frames drawn after the loss**: with `PB_ICON_SHOT` set (turns off N120's present skip, so every frame is
   rendered) and `shot` steps, compared with `harness/shotdiff.py` against a loss-free run (two loss-free runs are
   pixel-identical). Loop numbers run ahead of game frames by the loops a loss skips (1 at the first loss, 2 at the
   second here; it varied between 2 and 3 for the second loss across runs, timing), so pairs are offset. After the
   first loss (a grey fade) every shot matched, including the first frame on the new device. After the second (the
   party-setup envelope) the first frames lack some models (the envelope, the Goomba card) while the pipelines are
   rebuilt in the background, as on a cold start; 8 frames later and on (1510, 1600, 2390) the frames are
   pixel-identical to the loss-free run.
3. **Dawn validation on** (`skip_validation` commented out temporarily, not committed): the scripted run with 3 losses
   and a non-scripted run (vsync, RmlUi overlay documents) with `lose@100,lose@300`: no WebGPU error, so no object of
   the old device is used on the new one (Dawn reports cross-device use as a validation error).
4. **Give-up guard**: `lose@100,200,300,400`: three recoveries, then `[FATAL | aurora] Device lost 4 times in 10 s:
   giving up`.
5. **Browser**: `WEB_FAST=1 tools/web.sh` builds; `tools/web_check.mjs` ran the same 2400-frame script in headless
   Chromium: `identical: 2400 frames` against the desktop run (no loss there).
6. `tools/check.sh` passes.

## Tried and dropped

- Recovering at the top of `aurora::begin_frame`: right place, but 0010 rewrote that function, so the hunk broke
  0010's reverse check (see above). `aurora_update` runs every loop in both loops (`main.c`, `launchUILoop`).
- Comparing shots at the same loop numbers: they differed from the first loss on, which was the loop/frame offset,
  not rendering (the hash log is per game frame and identical).

## Not verified

- **A real device loss** (driver reset, GPU hang, TDR): only `ForceLoss`. A real loss may also fail the surface or
  the adapter request; then the recovery's ASSERT stops the game with "Failed to make a new GPU device" (no
  worse than before). Forcing a real radv reset would also reset every other slot's game on this machine.
- **Android, iOS**: not run (no device here; the emulator's GPU is the host's). The deferral while backgrounded
  can't be seen on the desktop: aurora's backgrounded flag (0010's event watch) is set only on Android/Apple, so the
  desktop `bg` step leaves the window presentable and the recovery ran while "backgrounded". Builds for them were
  not redone: the patch has no platform code (check.sh's Linux build only).
- **The browser's own device loss**: not testable (emdawnwebgpu has no `ForceLoss`; `device.destroy()` reports
  `Destroyed`, which the callback ignores on purpose). The recovery code is the same there, but a browser loss
  (GPU process crash) was never produced.
- **RmlUi on screen after a loss**: no screenshot of RmlUi (aurora_capture_frame takes the EFB before RmlUi, and the
  rules exclude gamescope captures). Covered only by the validation run (no errors) and by the code path.
- **EFB copies across a loss**: copy textures are dropped; a copy sampled before the game copies again shows as
  whatever its RAM buffer holds for that frame. Not seen in the shots, not looked for specifically.
- **An online match with a loss**: not run (a loss only skips loops, as a hold does; the netplay side is untouched).
- **Windows/D3D12**: not built here (no MSVC; MinGW toys only).
