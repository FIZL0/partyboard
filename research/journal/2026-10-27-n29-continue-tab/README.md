# N29: Online menu Continue tab

## What landed

- `src/port/ui/online.cpp` Continue tab (was a list of `<config>/online/*.json` by file name and mtime that could
  not start anything):
  - Lists N9's saves from `pbnet_save_list(<user dir>)` (only saves pbnet can read; newest first). Row: board name,
    `Turn X/Y`. Details pane: board, turn, save date (the save's `time`, not the file mtime), each seat
    (name or CPU, character), the seats this device's players hold marked "(this device)".
  - "This device" = a seat whose key in the save file's `keys` is one of the first `local` profile keys
    (`<config>/online.json`). `pbnet_save_list` doesn't list keys, so `load_save_keys` reads them from the file
    (nlohmann, in C++; no change to `net/`).
  - Pressing a save: modal Host Match / Delete Save / Cancel. Host Match = `pbnet_lobby_host` with the usual
    host setup plus `"resume": <path>` (`setup_json(..., resume)`), then the LobbyWindow as for Host. Guests join
    with the code as usual; pbnet (N9 `Host::resume`) seats them by player key. Delete Save asks again, then
    `pbnet_save_delete` and the tab is rebuilt.
  - In a lobby: "Back to Lobby" on top (hosting a continue leaves that lobby, as Host does).
- LobbyWindow, for a continued room (`room.checkpoint` present, `View::resume`):
  - line "Continuing a saved match at turn X of Y.";
  - a saved player's seat the computer holds shows "<name> (CPU) - <character> (<level>)" (pbnet's name);
  - rules and characters locked (pbnet refuses changes anyway); the host's seat pane has no "Open" (pbnet refuses
    it on a continue), only the CPU level, and says the player gets the seat back by key.
- `online_model`: `SaveInfo`/`SavedSeat`, `parse_saves`, `load_save_keys`, `own_seats`, `View::resume*`,
  `setup_json` resume parameter. Removed the old `Save`/`list_saves` (only the Continue tab used them).

## How it was checked

Real game with the disc (`PB_DISC`), own user dir under the worktree (vsync off, profile relay off, key `hostkey`),
two hand-written online saves (format 1, started room, fake 64-byte blob). A temporary hook in `ui::update`
(reverted) opened OnlineWindow on the Continue tab and pressed through it, dumping the DOM and `pbnet_lobby_json`:

- List: `Boo's Haunted Bash  Turn 5/30`, `Goomba's Greedy Gala  Turn 12/20` (newest first).
- Details for Boo: `1P: Other - Wario / 2P: Hosty - Donkey Kong (this device) / 3P: CPU - Daisy / 4P: CPU - Waluigi`.
- Delete Save → confirm modal → Delete: file gone, list rebuilt with only Goomba.
- Goomba → Host Match: lobby `open`, short code obtained, room id `goomba-match`, `checkpoint` turn 12/20, 1P
  `Hosty` (seated back by key), 2P `Guesty (CPU)` with key `guestkey`; status lines "Everyone is ready." and
  "Continuing a saved match at turn 12 of 20.".
- Scratch guest (C on the real `libpbnet.a`, key `guestkey`, relay off) joined by the long code: seated in 2P
  (`seats: [1]`, Luigi kept), host lobby showed `2P Guesty2 - Luigi`.
- `tools/check.sh` passes.

## Tried and failed

- Pressing the modal's buttons with a Submit event does nothing: `Button` listens to Click (SelectButton is the other
  way round). Used Click for `.modal-btn` in the hook.

## Not verified

- **Start of a continued match** from this UI: the test saves have a fake blob (no real board checkpoint exists:
  reaching a checkpoint in-game needs the minigame path, N43), so Start was not pressed. N9's
  `netplay_match_continue` path is what runs after Start; untouched here.
- **By eye / real input**: no screenshot, no pad/mouse input; presses were synthetic events from the hook.
- **Rules/characters locked look** in the Rules tab and the seat pane without "Open": built, not dumped.
- **Two machines / phones**: loopback only.
- **Non-ASCII user dir** (path through `u8string`): not run.
