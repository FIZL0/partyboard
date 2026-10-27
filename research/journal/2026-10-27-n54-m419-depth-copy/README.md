# N54: m419 (ovl 27) loses the GPU: Z24X8 GXCopyTex went through the color blit

## Cause

m419's layer hook `fn_1_57C` copies a 192x192 EFB rectangle every frame twice, to 96x96 textures:
RGB5A3 (color) and **GX_TF_Z24X8 (depth)**. In aurora (`lib/gfx/common.cpp`, the pass resolve):

- `tex_copy_conv` had a conversion pipeline for one depth format only, `GX_TF_Z16`. So for Z24X8
  `needs_conversion` was false, and `GXCopyTex` made a plain RGBA8 render texture.
- At the resolve, `isDepth` picked the **depth** view as the source, and because 192 != 96 (`needsScaling`)
  it went to `tex_copy_conv::blit`: the passthrough pipeline, whose layout wants a filtering sampler plus a
  `texture_2d<f32>`, given the depth bind group. (Its colour target is the surface format, not the RGBA8 dst.)
- Release builds (`NDEBUG`) turn on Dawn's `skip_validation` and `disable_robustness`, so the draw went to Vulkan
  with a mismatched descriptor set. radv: GPUVM fault, VK_ERROR_DEVICE_LOST, aurora FATAL. A GPU reset takes
  down every context, which is why parallel partyboard runs died at the same moment.

`GXSetZTexture` (suspected in N40) is an empty TODO in aurora (GXTev.cpp) and plays no part.

## How it was found (no GPU fault on purpose)

A device loss also kills other slots' games, so I did not reproduce the crash itself. Instead:

1. Temporary hook in bootDll (env `PB_N54_TMP`, with `backend.skipBootSequence`): 4 COM players (characters 0-3),
   `GWSystem.mg_next = omMgIndexGet(DLL_m419dll)`, `omOvlCallEx(DLL_m419dll, ...)`. Reaches ovl 27 straight from
   boot, faster than Mini-Game mode's menus. Reverted.
2. Temporarily took `"skip_validation"` out of aurora's Dawn toggles (gpu.cpp). Reverted.
3. Run: right after `m419Dll.so prolog end`:
   `[FATAL | aurora::gpu] WebGPU error 2: Bind group layout [BindGroupLayout "TexCopyConv Bind Group Layout"] of
   pipeline layout ... does not match layout [BindGroupLayout "TexCopyConv Depth Bind Group Layout"] of bind group
   ... While encoding [RenderPassEncoder "TexCopyConv Pass"].Draw(3, 1, 0, 0).`

## Fix: `cmake/patches/aurora/0002-gfx-depth-copy-formats.patch`

- `tex_copy_conv.cpp`: depth conversion pipelines for every other format `is_depth_format` knows: Z24X8 (RGBA8:
  R = z[23:16], G = z[15:8], B = z[7:0], A = 1), Z8 (z[23:16] as I8), Z4 (z[23:20]), Z8M (z[15:8]), Z8L (z[7:0]) and
  Z16L (z[15:0], IA8 laid out like aurora's Z16). They all use the existing `gx_z24` (textureLoad, nearest), so a
  scaled copy is fine.
- `common.cpp`: a depth resolve without a conversion pipeline is now a `Log.fatal` naming the format, not a
  blit/copy of the wrong view. None should reach it now, since every depth format has a pipeline.
- Applied at configure like 0001 (`cmake/aurora_patches.cmake`, comment added). Checked: applies forward to a clean
  upstream `lib/gfx` and reverse-checks on the patched tree, so the configure skips it.

## Checks (GMPE01, own user dir, vsync off)

- Validation on + fix: m419 plays to its end with 4 COMs. ovl 27 then returns to ovl 1 (event 1, i.e. the hook's
  boot); no WebGPU error in the whole minigame (both copies run every frame), within one 90 s run.
- Shipping config (validation skipped again) + fix: the same run, the same end, no FATAL. `journalctl -k`: no
  GPUVM/amdgpu fault in the 10 minutes around the runs.

## Not verified

- **What the copied depth texture holds.** aurora's `GXSetZTexture` is a no-op, and m419 samples TEXMAP1 only for
  the Z texture (stage 1 is PASSCLR), so the content is not visible. The channel layout for Z24X8 and the
  CTF formats follows the GC texture layouts as I understand them and was not checked against hardware/Dolphin. The
  minigame's afterimage/depth effect therefore still looks the way aurora draws it without Z textures. No screenshot
  was compared.
- **The crash itself with the unfixed build** was not rerun on purpose (a GPU reset kills other slots' games).
  The evidence is the validation error at the same place (m419's first frames), and that the fixed release build
  no longer faults.
- **m419 reached through a party or Mini-Game mode's menus** (N42's path): not run; the direct boot hook only.
- **Two peers / lockstep**: not run; the fix is renderer-only and does not touch game state or the hash.
- **MSAA**: depth copies from a multisampled EFB are still `Log.fatal` in aurora (unchanged); default config was used.
- **Other minigames copying depth formats**: none searched for. The new pipelines cover them if they exist.
- Windows/Android/Apple: not built (no toolchains).
