# N5: lockstep in the game loop

## What was done

- `src/port/netplay.c`:
  - `netplay_tick_begin()`: the per-frame gate. With no pbnet session or under `--script`, it returns TRUE
    (the game runs as before). With a session it:
    1. sends `netplay_hash()` via `pbnet_set_hash` when `pbnet_frame() % 30 == 0`, once per frame number
       (a WAIT repeats the call for the same frame and the same state);
    2. logs every `pbnet_poll_desync` with `OSReport` (handling a desync belongs to a later task);
    3. reads this machine's 4 controllers with the real `PADRead` (bypassing the `--wrap`) and passes all 4
       to `pbnet_step(local, 4, out)`. pbnet takes the first `local_seats()` of them, so **local controller
       i plays this machine's i-th seat**. A local pad with `err != NONE` is sent as an idle, connected pad:
       a seat stays connected for the whole match, and a local hiccup (`PAD_ERR_TRANSFER`) does not reach
       the other peers. `extButton` is zeroed (pbnet does not send it);
    4. latches buttons: pbnet takes the local pads only at the first call for a frame (N3 journal), so
       buttons seen in later calls for the same frame (the WAIT calls) are ORed into the next pads it
       takes. A tap made only while waiting is not lost. Sticks and triggers are not latched (the latest
       value wins);
    5. READY: keeps the 4 pads and returns TRUE. WAIT: returns FALSE. OFF (session ended between calls):
       TRUE.
  - `netplay_pad_read(status)`: what the game's `PADRead` gets while `netplay_active()`: the script pads,
    the last READY frame's pads (idle connected pads before the first READY), or the local controllers.
    `__wrap_PADRead` now calls it, so `sreset.c`'s `PADRead` sees the same pads as `pad.c`.
  - `netplay_lockstep()`: `pbnet_lockstep_active()` as a BOOL.
- `src/game/pad.c` `PadReadVSync`: `if (netplay_active()) RumbleBit = netplay_pad_read(status); else`
  the original `PADRead`. The `--wrap` already redirected this call on ELF; the explicit hook is what the
  brief asked for and also works where there is no `--wrap` (Windows, Apple; see cmake/netplay.cmake).
- `src/game/main.c` `game_main`: right after the aurora event pump,
  `if (netplay_active() && !netplay_tick_begin()) { ui_update(); frame_limiter(); continue; }`. A WAIT
  frame pumps events (window stays responsive, exit works), updates RmlUi and sleeps to the 60 Hz pace,
  but runs no game code and does not call `aurora_begin_frame`/`aurora_end_frame`. The frame limiter
  also ignores the turbo key (TAB) while a lockstep session runs.

### Timing

`pbnet_step` for frame N runs before tick N; its pads are stored, read by `PadReadVSync` at the end of
tick N (`HuSysDoneRender` → `SwapBuffers` → `VIWaitForRetrace` → the post-retrace callback, see
`src/port/stubs.c`) and copied to `HuPad*` by `HuPadRead` at the start of tick N+1. That is the same
one-tick lag the game has offline, and the same on every peer. Other `VIWaitForRetrace` calls inside a
tick (THP decode loop, boot) read the same frame's pads again; every peer makes the same calls, so it stays
deterministic. If `HuSoftResetButtonCheck`/`HuDvdErrWait` skips a tick after a READY, that frame's pads
are dropped on every peer alike.

### Why "keep the last frame" means "do not present"

aurora has no re-present call, and an empty `aurora_begin_frame`/`aurora_end_frame` pair does not show
the last frame: the first render pass clears the EFB (`clearColor = true`, `extern/aurora/lib/gfx/common.cpp`),
and the present source is that EFB (`webgpu::present_source()`). Skipping the pair leaves the
last presented image in the swapchain. The cost: RmlUi is updated but not drawn during a WAIT (toasts
and a future "waiting for <name>" overlay freeze). Filed as **N17**.

## Tests

- `tools/netplay/tick_gate_test.{c,sh}` (now run by `tools/check.sh`): builds the real `src/port/netplay.c`
  with a fake pbnet, a fake `PADRead` and stub game state, under ASan/UBSan. Checks: passthrough with no
  session; idle connected pads before the first READY; READY pads reach `netplay_pad_read`; a local pad
  error goes out idle and connected; WAIT returns FALSE and keeps the last frame's pads; a tap made only
  during a WAIT goes out with the next frame's pads, and only once; the hash is sent once for frame 30
  across three WAIT calls; a session that ends falls back to the local controllers.
- Mutation check: removing the latch OR makes the test fail (`taken[0].button == PAD_BUTTON_A`).
- `tools/check.sh` passes (cargo tests, the C test, the full CMake build with no new warnings in the
  three files). `nm -D libdol.so` exports `netplay_tick_begin`, `netplay_pad_read`.

## Tried / failed

- Nothing failed. First draft clamped the local pads with `PADClamp` before sending; dropped it, since
  `PadReadVSync` clamps the received pads on every peer anyway.
- First draft of the `check.sh` line ran the C test before the submodule init; moved after it (the test
  needs aurora's headers).

## Not verified

- **No game run**: `orig/GMPE01_00/` holds no disc image, so the game was not started; neither offline
  play with these hooks nor a lockstep session ran. Offline is unchanged by inspection: every hook is
  behind `netplay_active()` (and the frame-limiter change behind `netplay_lockstep()`), which are false
  offline.
- **No real lockstep session**: a session can only be started from Rust (`lockstep_c::start`), and
  nothing does yet (match start is N8, transport N2). The gate was checked against a fake pbnet only,
  so the end-to-end path (two peers, real `pbnet_step`) did not run.
- That the window keeps showing the last image when no frame is presented for a while: true for
  FIFO swapchains on the usual compositors, not checked on any platform here (no game run).
- Windows/Apple: without `--wrap`, `sreset.c`'s `PADRead` (soft-reset combo check) still reads local
  controllers during a session; only `pad.c` is hooked. Not built on those platforms.
- Seat mapping (local controller i → this machine's i-th seat) is a decision here; N6/N8 may want a
  user-chosen controller per seat.
