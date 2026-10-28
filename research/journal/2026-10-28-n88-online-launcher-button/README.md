# N88: 'Online' button on the launcher next to Play

## What changed

- `src/port/ui/prelaunch.cpp`: the launcher menu is now Play, **Online**, Settings, Quit. Play's body moved into
  `Prelaunch::launch_game()` unchanged. Online calls `launch_online()`: `launch_game()` and then
  `push_document(OnlineWindow)`, the same window Menu bar > Online opens. Menu bar > Online is untouched.
- No disc: Online sets `mOnlineAfterDisc` and opens the disc picker, like Play ("Select Disc Image") does. Once a disc
  passes verification, `Prelaunch::update()` continues to `launch_online()` by itself. Pressing Play clears the flag.
- Intro animation: Online takes `delay-2`. Settings, Quit, disc-info and version-info each move one step later
  (`delay-3` to `delay-6`). `.delay-6` (0.7 s) was added to `res/rml/prelaunch.rcss`.
- Keyboard and controller: `handle_nav_command` walks `mMenuButtons`, so Up/Down already reach Online. Touch works
  through aurora's finger → RmlUi path, like the other buttons.

## Bug found while testing (fixed)

The first version of the no-disc path continued in the same `update()` that sets `PartyBoard_IsGameLaunched`. That
update is still inside `launchUILoop`, before `HuSysInit`, so `HuCardInit` → `HuMemDirectMalloc` crashed on a null
heap (SIGSEGV in `HuMemMemoryAlloc2`, from coredumpctl). Now it waits until `PartyBoard_IsGameLaunched` was already
set when the update starts. `launchUILoop` exits on the frame the flag is set, so the next update runs from the game's
loop in `src/game/main.c`, after the heaps exist. Play never hit this: a person can't press it in that same frame.

## Decision: closing the Online window shows the menu bar

Closing an `OnlineWindow` runs `Document::pop()` → `show_top_document()`. The launcher has closed by then, so the
document under it is the hidden `MenuBar`, and the menu bar opens (Escape / B closes it). Menu bar > Online → close
leaves you in exactly the same state. I kept this rather than adding a launcher-only close path: Lobby > Leave
re-creates a plain `OnlineWindow` anyway, so a flag on the window would get lost.

## How it was checked (with the disc, PB_DISC)

Temporary harness: `harness/n88_test.inc`, included from `ui::update()`, plus a `PB_N88_FAKEPICK` hook in
`open_iso_picker` that skips the native file dialog. The diff is in `harness/test-hooks.diff`. Run with
`harness/run.sh <name> <script> [nodisc]`. Each run gets its own XDG_DATA_HOME/HOME and `online.json` with
`relay: off`. The harness uses SDL_PushEvent for key, gamepad and finger events, and its DOM dumps print the document
stack, the focus and `pbnet_lobby_state()`. Results:

| run | input | result |
| --- | --- | --- |
| kbd | Down, Return; then tap Host Game | Down moved focus Play → Online. The OnlineWindow was on top and visible, with the launcher pending close and the game launched. Host → LobbyWindow, `lobby_state 2` (Open) |
| pad | DPAD_DOWN, SOUTH; DPAD_DOWN, SOUTH | Same as kbd, with the Online window and Host Game both driven by the gamepad |
| touch | finger tap on Online; Escape | OnlineWindow on top. After Escape, the MenuBar is visible (see decision above) |
| play | finger tap on Play | No OnlineWindow, menu bar hidden (Play unchanged) |
| nodisc | no PB_DISC; tap Online → fake pick of the disc | Verification modal, then the game boots and the OnlineWindow opens by itself. Host → `lobby_state 2` |
| kbd2 | aurora_capture_frame at frame 400 | The game ran under the Online window (Hudson boot logo, on the way to the title) |

Harness pitfall: `Element::GetAbsoluteOffset` ignores CSS transforms. The launcher `menu` has
`transform: translateY(-50%)`, so the first touch taps landed below the buttons, on `content#root`. The harness now
scans for a point where `GetElementAtPoint` returns the button.

## Not verified

- Real touchscreen / Android / iOS: only SDL_PushEvent finger events on desktop (no device run in this task).
- The real native disc picker (an xdg portal dialog would open on the user's screen). It was skipped with the
  `PB_N88_FAKEPICK` hook, which calls the same `file_dialog_callback`.
- Pixels of the Online window drawn over the title: `aurora_capture_frame` grabs the EFB before RmlUi draws. That the
  window was on top and visible was checked with DOM dumps only.
- The disc-picker → hash-mismatch warning → Online path. Only a verified disc was used.
- The small-height (mobile, ≤640dp) layout with four buttons was not looked at.
