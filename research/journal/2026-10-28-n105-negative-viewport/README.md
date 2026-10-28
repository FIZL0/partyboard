# N105: m411's negative viewport in the browser

N97 run 3 found both browser tabs dying 0.3 s into m411 (overlay 19) on a fatal WebGPU validation error:
`Viewport bounds (width: -20, height: 938) contains a negative value` at `SetViewport(1284, 20, -20, 938)`.

## Cause

m411 is a four-way split screen that it opens and closes by moving the split point. `fn_1_8DBC`
(`src/REL/m411Dll/main.c:2340`) hides cameras 4, 8 and 16 with viewports of width or height -10
(`Hu3DCameraViewportSet(4, 642, 10, -10, 469, ...)`) and scissors of -10, and `fn_1_8A80`'s `630 - arg0` /
`470 - arg1` sides go negative the same way while the split point is past the edge. On the GameCube the
viewport is just a scale (a negative one mirrors) and the scissor's right edge (left + width - 1) lands left of
its left edge, so nothing is drawn. aurora already decodes that scissor as width 0
(`lib/gx/command_processor.cpp`, `std::max(rt - lf + 1, 0)`), but the viewport went to the render pass with its
negative side. Desktop release builds turn Dawn's validation off (`skip_validation`) so it passed there; the
browser's WebGPU validates and stops.

## Fix

`cmake/patches/aurora/0011-gfx-negative-viewport.patch`: where aurora records `SetViewport` into the render pass
(`lib/gfx/common.cpp`, the pass's command loop), a negative width or height becomes the same rectangle with
positive sides (`left += width; width = -width`, and the same for top and height). That covers every viewport
source (GX's XF registers, RmlUi) on every platform. The mirroring is lost. That makes no difference for m411,
whose scissor for those cameras is empty, and no game code mirrors a viewport it draws through.

Where not to fix it: skipping the camera in `Hu3DExec` (`src/game/hsfman.c`) would also skip what that loop
does to game state per camera (`Hu3DMotionExec`, `Hu3DCameraMotionExec`, `data->tick++`, `attr |= 0x800`), so it
would change the hashes. Clamping in m411 would be a decomp-side change for a renderer limitation. So no
`src/game/hu3d.c` (named in the brief) was needed.

The patch's lines don't overlap any earlier patch's context: on a clean `git archive` of aurora's HEAD all of
0001..0011 apply in order and each one still passes `git apply --reverse --check` afterwards (the configure's
skip test).

Other RELs: grep for literal negative sizes in `Hu3DCameraViewportSet` / `Hu3DCameraScissorSet` /
`GXSetViewport` finds only m411's. Computed sizes (m411's `fn_1_8A80`) are covered by the same aurora fix.

## Checks

Desktop A/B (`build/n105-fco`, the desktop preset with `-DPB_FCO_BOOT`, N106's `PB_FCO_OVL=19` jump into m411
with 4 COMs, `--script` holding `end 5000`, `--turbo`, `--hash-log`). Each build was copied to
`build/n105run/<variant>`:

| build | Dawn validation | result |
| --- | --- | --- |
| base (no 0011) | off (release default) | 5000 frames |
| base | on (`skip_validation` commented out) | aborts at frame 6: `[FATAL | aurora::gpu] WebGPU error 2: Viewport bounds (width: -32.000000, height: 1500.800049) contains a negative value ... SetViewport(2054.400146, 32.000000, -32.000000, 1500.800049, ...)`. This is the browser's error on the desktop (642 / -10 at a 3.2× render scale) |
| new (0011) | off | 5000 frames |
| new | on | 5000 frames, no WebGPU error |

The hash logs of base/off, new/off and new/on are byte-identical (md5 e2f65bfe238d75bcd9f47c41a3ecda16).

Lesson: desktop with Dawn validation on reproduces browser-only WebGPU validation failures without a web build
(see also [[partyboard-gpu-fault-repro]]).

Browser (`WEB_FAST=1 tools/web.sh` with the patch, plus a temporary bootDll edit, not committed: `#define PB_FCO_BOOT` and
`|| getenv("PB_FCO_OVL")` in the skipBootSequence test, since the web build has no config switch for it). Ran
`node tools/web_check.mjs $PB_DISC build/n105run/web-new 420 'env=PB_FCO_OVL=19,PB_FCO_HUMAN=0&args=--script
/test/m411.script --turbo --hash-log /tmp/hash.log&fetch=m411.script'` with `WEB_FILES=build/n105run/m411.script
DUMP_FILES=/tmp/hash.log STOP_ON="script ended"` (headless Chromium on the GPU). m411 ran to `script ended at frame
5000` with no WebGPU error and `page errors: ""`. The screenshots show the four-way split. Every one of the 5000 hash
lines matches the desktop run's apart from `heap=` (wasm32 vs x86_64 heap layout), and the final hash is the same,
478BF82B091D262F.

What failed on the way: the first web build put the temporary `#define PB_FCO_BOOT` inside its own
`#if defined(PB_FCO_BOOT)` block (getenv undeclared). After the patch file had been moved away for the baseline build,
adding it back didn't re-run the configure (the glob's CMAKE_CONFIGURE_DEPENDS only lists the patches present at
the last configure), so the first "new" snapshots were unpatched. An explicit `cmake build/n105-fco` re-applied it.

## Not verified

- An unpatched browser run of m411: the failure was already seen in the browser in N97 run 3, and the desktop build with
  Dawn validation on reproduces the same error. Building a second web copy for it didn't seem worth it.
- Firefox (wgpu/naga): its viewport validation is the same rule in the WebGPU spec, but it wasn't run.
- A two-tab online match through m411 (web_pair.mjs): only a single-tab scripted run (4 COMs) was done. The fix is in
  the renderer only, and the game-state hashes are unchanged.
- Android/iOS: same aurora code, Dawn native with skip_validation in release; not run.
- m411 with human players (split points driven by input): COM-only run.
