# N51: the hold modal no longer opens the menu bar when it closes

Result: **fixed and checked in the game.** After a lockstep WAIT, the N10 hold modal ("Waiting for <name>") now
closes without showing the document under it. The menu bar stays hidden and `PADBlockInput` goes back to 0.

## The bug

`update_online_match` closed the modal with `Modal::pop` → `Document::pop` → `hide(true)` + `show_top_document()`.
During a match the only document under the modal is the hidden `MenuBar` (bottom of the stack), so every WAIT long
enough for the modal ended with the menu bar open (Settings / Online / Achievements / Quit). A visible document makes
`sync_input_block` call `PADBlockInput(1)`, so the local pads stayed blocked until the player closed the menu.
"Keep Waiting" (and Cancel/Menu, which call the same `keep`) had the same bug. N33 found it (filed as N45; it is this
task).

## The change

`src/port/ui/online.cpp`: `close_hold_modal(Modal &)` = `modal.hide(true)` + `input::sync_input_block()`. It is
used when the modal closes itself (the WAIT ends or the message changes), by "Keep Waiting" / dismiss, and by
"CPU Plays for <name>". In all of these the match keeps running under the modal. If the player had opened the menu bar
before the modal came up, the menu bar is still visible and stays visible, because nothing hid it.

Left on `Modal::pop` on purpose: "Leave Match" and "Save and Quit". They end the match and go back to the menus,
where showing the top document is the old, expected behaviour. N51 is about the match going on under the closed
modal, so these were not changed.

## How it was checked

N33's setup (research/journal/2026-10-27-n33-wait-still-check): `scratch_host.c` built against this branch's
`libpbnet.a` hosts the lobby and steps lockstep at 60 Hz. The game, inside `gamescope --backend headless`, joins it
through a temporary hook. `run.sh` waits for "entering board", sends `stall 6000`, and takes screenshots at +4.5 s
(modal up), +8.5 s and +10.5 s (after the WAIT). The temporary hooks are in `test-hooks.diff` (reverted). They:
log `any_document_visible` and the top document on every change, log each `PADBlockInput` call in
`sync_input_block`, and with `N51_KEEP` dismiss the modal after 60 frames (the "Keep Waiting" path; `Modal::dismiss`
was made public for this). A one-line hook that is not in the diff made `close_hold_modal` call `modal.pop()` when
`N51_OLD` was set, so one build ran both behaviours.

| Run | Log after the WAIT | Screenshot after |
|---|---|---|
| old (`N51_OLD=1`), 2 runs | `PADBlockInput(1)` at the modal, no `PADBlockInput(0)` after it; `any_document_visible` stays 1 | menu bar open (`old-after-wait.jpg`) |
| new, 2 runs | `PADBlockInput(0)`, `any_document_visible 0`, top = MenuBar (hidden) | board, no menu (`new-after-wait.jpg`) |
| new + `N51_KEEP` | dismiss → `PADBlockInput(0)`, `any_document_visible 0`; nothing shows up when the WAIT ends | board, no menu |

The modal that comes up at the end of every run is the host being killed by the script (a real drop). In the
`N51_KEEP` run, a new hold key produced a new modal each time, and each one dismissed cleanly.

Nothing failed along the way. The first run without the PAD log already showed the bug and the fix through
`any_document_visible`. The PAD log was added to see `PADBlockInput` itself.

## Not verified

- **Pads actually driving the game after the WAIT**: no controller is attached in gamescope headless ("No controller
  assigned"). What was checked is that `PADBlockInput(0)` is called and nothing visible remains. The game reading a
  real pad press after that was not checked.
- **"CPU Plays for <name>"**: it only shows on the host with a dropped guest. The game was the guest here, so this
  button was not pressed. It uses the same `close_hold_modal` as the paths that were checked.
- **Menu bar opened by the player before the modal appeared**: not run. Reading the code, `hide(true)` on the modal
  does not touch other documents, so the menu stays as the player left it.
- **Two real game instances / other platforms**: the peer was the scratch libpbnet host, on Linux only.
