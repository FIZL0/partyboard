# N146: home screen layout with Continue entries on a phone, and the version pill

User (2026-10-29, iPhone 15 Pro landscape, 0.1.1 with a Continue entry): every menu button's background shrank to a
~40 dp pill while its label ran on past it. The Continue button's detail wrapped one word per line and was cut off at
the top. The version pill read `0.1.1-0-dirty`.

## Cause of the collapsed buttons

The cause was **not** flex-basis 0, which the brief suspected. `prelaunch.rcss` styles no scrollbar. Without a style,
RmlUi's `scrollbarvertical` element has `width: auto`, so it is as wide as its container. A Continue entry makes
`#menu-list` (overflow-y: auto) overflow at phone height. Then the flex layout's second pass
(FlexFormattingContext::Format: `available = box_content_size - scrollbar_size`) leaves the buttons 0 px of
content. That gives a 44 dp button: 2x20 dp padding plus 2x2 dp border. Without Continue the list fits, no scrollbar
appears, and nothing collapses. That is why N141's tests (no Continue) never saw it.

Steps:
1. Repro with the N141 harness and fake entries: `#menu-list` 349 dp wide, buttons 44x57 dp, Settings/Quit outside
   the window.
2. Changed the list and logo to definite widths (`flex: 0 0 auto; width: 52% / 44%`, no flex-basis 0). The list
   came out right (357x290 dp, scroll height 451), but the buttons were still 44 dp.
3. `width: auto` on the buttons: no change. While testing it I found that only a rebuild that relinks copies
   `res/` into the build dir. Editing just the `.rcss` and running `cmake --build` leaves the old copy, so copy it
   by hand when testing.
4. Read RmlUi's FlexFormattingContext and found the scrollbar subtraction. The fix: a 6 dp
   `#menu-list scrollbarvertical` style (track, bar, no arrows; no horizontal bar). The buttons are now full width.

## What changed

- `res/rml/prelaunch.rcss`
  - `#menu-list` gets its own scrollbar style (the actual fix).
  - Landscape phones: `menu` has `align-items: stretch`. The logo is `flex: 0 0 auto; width: 44%` (centred with
    align-self). The list is `flex: 0 0 auto; width: 52%` and fills the column's height. The list is centred with
    `margin-top: auto` on the first button and `margin-bottom: auto` on the last. RmlUi only hands out auto
    margins when free space is positive, so an overflowing list starts at its top and scrolls. It is never pushed
    above the screen as `align-items: center` did.
  - Continue: a label span and a one-line detail span (`white-space: nowrap; overflow: hidden;
    text-overflow: ellipsis`). On landscape phones they sit in one row: `CONTINUE  Turn 5/20 - Yoshi, War…`.
    On desktop and in portrait the label sits over one ellipsized line. The text never wraps word by word.
  - A focused or hovered Continue now turns blue like the other buttons. `button.continue` had the same
    specificity as `:focus-visible` and came later, so a focused Continue kept its purple fill and got dark text.
- `src/port/ui/prelaunch.cpp`: `continue_detail` is one line, `Turn 5/20 - Yoshi, Wario, Peach, Luigi - Koopa's
  Seaside Soiree, file 1`. The turn and players come first, so they survive the ellipsis.
- `CMakeLists.txt`: `git describe` without `--dirty`. `-dirty` is appended only if
  `git diff-index --quiet --ignore-submodules HEAD --` fails, after `update-index -q --refresh`. The configure step
  patches extern/aurora and extern/musyx, which no longer count. A clean tag reads `v0.1.2` → "Version 0.1.2".
  Checked: the exit code is 0 with only the submodules modified and 1 with a tracked file modified. A `cmake -P` run
  of the string logic gives `v0.1.2-0-g…`+clean → `v0.1.2`, +dirty → `v0.1.2-0-dirty`, `-3-g…` → `v0.1.2-3`. The
  real configure in this worktree (with local edits) gives `v0.1.1-2-dirty`, which is correct.

Not changed: there is no Quit on iOS. That is deliberate (N133, `#if !(... TARGET_OS_IOS)`), so it is absent from
the user's screenshot.

## Tests

The harness is N141's (`../2026-10-28-n141-home-screen/harness/run.sh`, Hyprland resize, scale 1.6 = dp). Its diff
plus a dump of the layout boxes is in `harness/harness.diff` (add `src/port/ui/n141_harness.cpp` to files.cmake
PORT_FILES). `harness/fake_continue.cpp.txt` is a `PB_FAKE_CONTINUE=n` hook that adds n Continue entries to
the Prelaunch constructor:
1. a party autosave
2. a party in file 1
3. a story in file 2

The disc is the real `PB_DISC`. Screenshots are taken at UI frame 400 (`aurora_capture_frame_ui`). They show disc
art, so they are not committed.

| case | window, PB_SAFE | entries | #menu-list (dp) | buttons (dp) | result |
|---|---|---|---|---|---|
| iPhone 15 Pro landscape | 852x393, 59,0,59,21 | 0 | 357x290, no scroll | 349x57 | centred, all visible |
| | | 1 | 357x290, scroll 321 | 343x57 | Continue as one row; Quit (desktop only) below, scrolls |
| | | 3 | 357x290, scroll 451 | 343x57 | list starts at its top; swipe scrolls 161 dp; Settings tap opens Settings |
| iPhone 15 Pro portrait | 393x852, 0,59,0,34 | 3 | 353x505, scroll 597 | 339x87 (Continue), 339x65 | full width, ellipsized detail |
| small phone landscape | 667x375, none | 2 / 3 | 322x293 | 308x57 | full width, scrolls |
| small phone portrait | 375x667, 0,20,0,0 | 3 | 335x395, scroll 597 | 321x65 | swipe, then a Quit tap: game exits 0 |
| desktop | 1280x960 | 0 | 461x325 | 453x70 at 52,360 | same as N141's table (unchanged) |
| desktop | 1280x960 | 3 | 461x635, no scroll | 453x91 / 453x70 | all visible, detail on one line |

The "NO CONTROLLER ASSIGNED" toast in the shots comes from the fresh test user dir. It is not part of this task.

`tools/check.sh`: run by `agent-runner done`.

## Not verified

- **On a real iPhone/Android device**: none here. The desktop window at 852x393 pt and scale 1.6 lays out like the
  iPhone, but iOS was not run.
- **A real party autosave**: the entries were faked through the hook, not made by playing a scripted party to an
  autosave. The real path only feeds different numbers into the same `continue_detail` and markup.
- **The browser and Android builds**: not run. They use the same rcss and RmlUi.
- **Taps on every button at every size**: only the iPhone landscape Settings tap (after a swipe) and the
  small-portrait Quit tap (after a swipe) were done. The other buttons pass the harness's hit test at their centres
  where they are on screen.
- **The version string of a real tagged release build**: no tag was made. The logic was checked with `cmake -P` and
  the diff-index exit codes, as above.
