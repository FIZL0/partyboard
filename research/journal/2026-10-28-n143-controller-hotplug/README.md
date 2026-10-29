# N143: controllers connected after launch (iOS Xbox Series X pad)

User report (2026-10-28): the game was started on an iPhone without the Xbox Series X controller; connecting it
afterwards did nothing until the app was closed and reopened.

## How a controller reaches the game

1. SDL's joystick driver sees it. iOS: `SDL_mfijoystick.m` lists `[GCController controllers]` in `IOS_JoystickInit`
   and observes `GCControllerDidConnectNotification` / `DidDisconnect` (`queue:nil`) for later ones;
   `IOS_AddJoystickDevice` → `SDL_PrivateJoystickAdded` → `SDL_EVENT_JOYSTICK_ADDED` + `SDL_EVENT_GAMEPAD_ADDED`
   (a player index is given then: the lowest free one).
2. aurora's `window.cpp` takes `GAMEPAD_ADDED` → `input::add_controller` (logs `Added controller '…'`) →
   `apply_port_preferences`. SDL's player index is the port.
3. The game reads the port with `PADRead` (`src/game/pad.c` `PadReadVSync`, every frame; a port with no controller
   reads `PAD_ERR_NO_CONTROLLER`, `PADReset` is a no-op in aurora and the next read finds the controller). The home
   screen's RmlUi input (`src/port/ui/input.cpp`) takes every SDL gamepad event, whatever its port.

Nothing in steps 2-3 runs only at start: the same code runs at launch (SDL sends `GAMEPAD_ADDED` for each pad when
`input::initialize` starts the gamepad subsystem on aurora's first update) and on a hotplug. A relaunch only redoes
step 1 on iOS (SDL's MFi driver lists the controllers again).

## Desktop repro (works)

`PB_LIFECYCLE_TEST` got pad steps (`src/port/lifecycle.cpp`): `padon`/`padoff` attach/detach an SDL virtual Xbox
pad (vid 045e pid 0b13, as the user's), `pada`/`padrel` and `paddown`/`paddownrel` press its A / d-pad down,
`padlog` logs each port's controller index, the native button aurora reads and the game's `HuPadBtn`/`HuPadErr`,
`padlost` attaches the pad and flushes its `GAMEPAD_ADDED` (a lost add), `rescan` asks for the re-scan below,
`uishot` captures the frame with the menus. `harness/run.sh <name> <steps>` runs one copy with its own user dir.

Linux, Vulkan, `PB_DISC`, no pad connected at launch, prelaunch menu open:

- `padon` at loop 300: `Added controller 'N143 virtual pad' (instance 6 …)`, port 1. Home screen: the "Controller
  connected" toast, the "No controller assigned" warning gone, d-pad down moves the focus Play → Online
  (uishot loop 430). The first A press at 450 pressed Play.
- In game (title, behind the menu): A → `HuPadBtn[0] = 0100`, `HuPadErr[0] = 0`.
- `padoff`: `Removed controller`, ports empty, port 1 reads err -1 (no controller); `padon` again: instance 7, port 1,
  A read by the game. Disconnect / reconnect works.

So the desktop SDL + aurora + game path takes a late controller at once, on the home screen and in game. The miss is
in step 1 on iOS: SDL never sent `GAMEPAD_ADDED` (or aurora would have logged and used it: same code as at launch).

## Why iOS may miss it (from SDL 3.4.10's source; not seen on a device)

- SDL adds a late controller only from the `GCControllerDidConnectNotification` block. The connect is not polled:
  `IOS_JoystickDetect` is empty. If the notification is not delivered to SDL's observer, or comes while SDL's MFi
  driver skips the controller, SDL never sees the controller until the driver starts again (a relaunch).
- Things checked that do *not* explain it: SDL's iOS event pump runs `CFRunLoopRunInMode` every `SDL_PollEvent`
  (the game loop is on the main thread, started from a run-loop timer, not a main-queue block, so main-queue
  notifications are served); no `SDL_FlushEvents`/event filter anywhere in partyboard or aurora; iOS HIDAPI only
  claims Steam BLE controllers, so the `HIDAPI_IsDeviceTypePresent(XBOXONE)` skip can't fire; keyboard focus (which
  gates joystick *input*, not adds) is cleared on resign-active and restored on become-active.
- The Info.plist declares no game-controller support (`GCSupportsControllerUserInteraction`,
  `GCSupportedGameControllers`). Not added: no device to see what it changes, and it is not what SDL's own templates
  do. A candidate if the new log shows iOS sending the connect while SDL adds nothing.

## The fix: src/port/controller_watch.cpp (+ src/port/ios/ControllerWatch.m)

Called from `port_lifecycle_hold` (every loop, home screen and game) and set up after `aurora_initialize`:

- **Logs** (`partyboard::controllers` in partyboard.log): the port table every time a controller is added or removed
  (`1 controller(s); ports 1: 'Xbox Wireless Controller', 2: -, …`, and controllers with no port, which
  Settings > Controllers must assign), next to aurora's own `Added controller` / `Removed controller` lines; a joystick
  SDL has without a gamepad mapping (aurora ignores those); on iOS every `GCControllerDidConnect`/`Disconnect`
  notification with name, profile, count and thread, and the controllers present at start.
- **Lost add**: about once a second, a gamepad SDL lists but aurora never added (missing two checks in a row) gets its
  `SDL_EVENT_GAMEPAD_ADDED` again (once per id). Desktop `padlost`: `gamepad 7 … was never added: adding it now`,
  port 1, the game read A.
- **iOS re-scan**: when `GCController.controllers` has more controllers than SDL has gamepads for two checks in a row,
  SDL's gamepad + joystick subsystems are restarted, which is what a relaunch did for the user: aurora's controllers
  are removed first through `SDL_EVENT_GAMEPAD_REMOVED` (aurora closes its `SDL_Gamepad`s before SDL frees them),
  then `SDL_QuitSubSystem` + `SDL_InitSubSystem(GAMEPAD | JOYSTICK)`; SDL's MFi driver lists
  `[GCController controllers]` again and sends `GAMEPAD_ADDED` for each. At most one re-scan per GameController
  count (SDL may skip a controller on purpose; restarting every second would drop the others' input each time).
  Desktop `rescan` with a pad attached: `Removed controller`, `rescan: SDL lists 0 gamepad(s)` (the virtual pad goes
  with SDL's joystick subsystem), then `padon` → instance 8, port 1, the game read A. No crash, no stale handle.

No change to aurora was needed (its add/remove already log; the port is in the new port table). Offline play is
unchanged when nothing is missed: the checks only read lists.

## Not verified

- **On an iPhone/iPad**: no device here (`idevice_id`: none). Not seen: whether iOS sends
  `GCControllerDidConnectNotification` for the user's pad, whether SDL adds it, whether the re-scan picks it up, and
  whether the GameController count equals SDL's gamepad count with one Xbox pad (it should: the MFi driver adds every
  GCController on iOS except Steam virtual pads and HIDAPI-claimed ones, neither possible here). The user's
  partyboard.log after a late connect now says which step failed: no `iOS: GameController connected` line = iOS sent
  nothing to the app; that line but no `Added controller` = SDL missed it (then `re-scanning controllers` should
  follow within ~2 s and `Added controller` after it); `Added controller` with `no port` = a port preference.
- The re-scan with a *real* controller surviving it: on the desktop the only pad was SDL's virtual one, which SDL's
  restart deletes, so "SDL lists the pad again after the restart" was not seen (Linux real pads: none here).
- Android: not run (the lost-add check is platform-neutral; no Android report).

## Runs and builds

- Final desktop run (all steps, current build, 70 s): `harness/final-run.txt`. Screenshots were looked at, not
  committed (game frames are disc art).
- IPA: `tools/ios-ipa.sh` from scratch in this worktree (28M, 93 dylibs); `tools/ios-ipa-check.py` rc 0; libdol
  exports `PartyBoard_iOS_ControllerWatchStart`/`ControllerCount` and links GameController.framework. SDL is static in
  libdol only (the app binary imports SDL from it), so the re-scan restarts the one SDL the game uses.
- `tools/check.sh`: run by `agent-runner done`.
