# N33: N19's lockstep WAIT still frame, checked in the game

Result: **N19 works as designed.** During a real lockstep WAIT the last tick's image stays under the toasts and the
N10 "Waiting for <name>" modal. It is not black, not flipped and not offset, it is bit-exact, and it is right with
FIT, with STRETCH and with MSAA 4x. The tick after the WAIT draws normally. The per-tick EFB copy makes no
measurable difference to frame time. No code change was needed. One unrelated bug turned up (N10's modal opens the
menu bar when it closes) and is filed as **N45**.

## Setup (how to repeat it)

- Real game, real disc (`PB_DISC`, GMPE01 rev 1), Linux RelWithDebInfo build of this branch.
- **Screenshots without touching the desktop**: the game runs inside `gamescope --backend headless -w W -h H -W W -H H`.
  `GAMESCOPE_WAYLAND_DISPLAY=gamescope-N gamescopectl screenshot /abs/path.png` saves the composited output, RmlUi
  included. The plain `gamescopectl screenshot` with no path fails ("Unsupported extension"). Use the newest
  `gamescope-N` in `/run/user/$UID`, because killed instances leave their sockets behind. Unlike the private
  Xwayland route, no window ever shows on the user's screen.
- **A real lockstep peer**: `scratch_host.c` (here, not built by anything) is a C program linked against the real
  `build/.../pbnet-cargo/release/libpbnet.a`. It hosts a lobby (relay off, player "Hosty", disc `GMPE01-01`),
  writes its ticket to a file, starts once the guest is ready (seed 12345, board 0, open seats become CPUs), then
  calls `pbnet_step` with idle pads at 60 Hz. When it reads `stall <ms>` on stdin, it stops stepping for that long,
  so the game holds a genuine `PBNET_WAIT`.
- **Game side**: a temporary hook (`test-hooks.diff`, reverted) adds the following:
  - `ui_update` joins the ticket from `$N33_CODE` with `pbnet_lobby_join` and calls `pbnet_lobby_ready(1)` while
    the lobby is open. A settings change clears ready, so a one-shot ready did not survive the host's
    `pbnet_lobby_settings`.
  - `$N33_MSAA` goes into `AuroraConfig.msaa`. Party Board never sets it, so MSAA is otherwise always 1.
  - `main.c` times `netplay_still_keep`, counts WAIT frames, and logs ms/tick every 600 ticks. `$N33_KEEP` forces
    the copy in non-lockstep runs and `$N33_TURBO` sets `disableFrameLimiter`.
  - The match is started by the real path: the lobby goes STARTED, then `netplay_lobby_poll` reads the room and
    calls `netplay_match_start`. The log shows "entering board 0".
- Each run has its own user dir with `video.enableVsync: false`, `backend.skipPreLaunchUI: true` and
  `backend.skipBootSequence: true` (plus `video.lockAspectRatio` / `video.enableFullscreen` per variant).
  `run-scripts.sh` holds the three driver scripts.

## What was seen

Each variant: wait for the board, take a screenshot, `stall 6000`, then screenshots at +0.5 s, +2 s and +4.5 s
(the N10 modal appears at about 3 s), +7.5 s (after the WAIT) and +8.5 s. The game logged
`first tick after 357 WAIT frames` for the 6 s stall (357 frames ≈ 5.95 s at 60 Hz).

| Variant | Under the toast (0.5 s, 2 s) | Under the modal (4.5 s) | After |
|---|---|---|---|
| 1280x960 window (4:3, the default) | last tick, unchanged | still frame under the blurred modal | game moves on (Yoshi's pose changed) |
| 1920x1080 fullscreen, FIT (`lockAspectRatio` on) | pillarboxed 4:3, bars black, image inside the game area | same | right |
| 1920x1080 fullscreen, STRETCH | stretched to 16:9 the same way as live frames | same | right |
| 1920x1080 fullscreen, STRETCH, MSAA 4 | right, edges as live | same | right |

Numbers behind "right":

- **Frozen and exact**: in every variant, the 0.5 s and 2 s WAIT screenshots are pixel-identical (`compare -metric AE`
  = 0) over the game area above the toast.
- **Not offset or rescaled**: the last live screenshot against the WAIT screenshot is 99.6–99.8% bit-identical (fuzz 0)
  above the toast. The pixels that differ are the parts that animate between the screenshot and the stall (ferris
  wheel, Toad balloon, Peach's idle pose, Mario turning), and the count barely changes from fuzz 0 to fuzz 3%. An
  offset or flip would change nearly every edge pixel. This also confirms that the `GX_TF_RGB565` copy keeps full
  8-bit colour in aurora (only alpha is forced to 1), as N19 assumed.
- Short natural WAITs (2–3 frames) happen on loopback too (`first tick after 2 WAIT frames`). They now show the still
  frame instead of a cleared EFB.

First try that was wrong: the first FIT/STRETCH runs at 1920x1080 were windowed. gamescope then scaled the
1280x960 window, so both came out pillarboxed and tested nothing about the viewport policy. They were redone
fullscreen (`video.enableFullscreen: true`), where STRETCH really stretches.

## Cost of netplay_still_keep

- **Frame time**: turbo `--script tools/netplay/mash-a.script --match 0@900` run (deterministic, the same 13200
  frames each run, board 0 from frame 900), 1280x960 window, RX 9070, runs alternated. Mean over ticks
  1200–13200:

  | copy each tick | ms/tick |
  |---|---|
  | off (3 runs) | 0.571, 0.562, 0.557 |
  | on (2 runs, 600 keeps per 600 ticks) | 0.559, 0.546 |

  The difference is within noise, below about 0.02 ms per tick, against a 16.7 ms lockstep tick.
- **Game-thread CPU time inside the call**: 0.2–0.4 ms in the turbo runs, and 0.43–0.63 ms average in the lockstep
  runs at 1280x960 and 1920x1080 (up to ~1.3 ms in the windows that include the board load). Most of this is
  `GXCopyTex` → `aurora::gx::fifo::drain()` processing the frame's queued GX commands, which `aurora_end_frame`
  would do anyway. The unchanged ms/tick above shows it moves work rather than adding it.
- GPU time of the resolve alone was not isolated (no timestamp queries). See Not verified.

## Found on the way (not N33's code)

- **N45 (filed)**: when the WAIT ends, the N10 hold modal closes itself through `Modal::pop` → `Document::pop` →
  `show_top_document()`. That shows the hidden MenuBar at the bottom of the document stack. After every WAIT long
  enough for the modal, the menu bar (Settings / Online / Achievements / Quit) is open mid-match, and since a
  visible document sets `PADBlockInput`, the local pads are blocked until the player closes it. It showed in every
  variant's "after" screenshots.
- Code reading only: aurora's `resize_swapchain` calls `gx::clear_copy_texture_cache()` when the surface size
  changes. If the window is resized or toggled to fullscreen during a WAIT, the still frame becomes the zeroed
  `calloc` buffer (black) until the next tick. This is the case N19's journal named as "copy cache dropped". It is
  harmless, but it is now known when it happens.

## Not verified

- **Resize / fullscreen toggle during a WAIT** (the black-until-next-tick case above): gamescope headless gives no
  way to resize the client window from outside, so it was not run.
- **GPU time of the copy alone**: no GPU timestamp queries in aurora. Only total frame time was compared, and at
  ~0.55 ms/tick on an RX 9070 the copy may be hidden behind CPU work. A weak GPU (phones, the Radeon 610M iGPU) was
  not measured.
- **Higher internal resolution scale** (`game.internalResolutionScale` > auto): not run. The copy size follows
  aurora's `scale_copy_dst`, so it should scale like any EFB copy.
- **Two real game instances**: the peer was the scratch libpbnet host, not a second game. The game-side code under
  test (`netplay_tick_begin` WAIT → `netplay_still_draw`) is the same either way.
- **WAIT before the first kept tick** (the clear-colour case): it can only happen on frame 0 of a match. It was not
  forced.
- **Other platforms** (Windows/macOS/Android/iOS, Vulkan vs. other Dawn backends): Linux/Vulkan only.
- **By eye on a real display**: screenshots come from gamescope's composited output, not the user's monitor.
