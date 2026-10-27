# N35: N27's lobby-to-match glue with a GMPE01 disc

## Outcome

On `6c37c4f6` (relwithdebinfo, Linux), two processes hosted and joined a lobby in the Online menu, the host
pressed Start Match, and both entered the board through `netplay_lobby_poll`. **There was no desync**: the
hash logs from the match's frame 0 are identical for **41179 frames** (about 11.4 min at 60 fps). Human seats
were played from the keyboard on both peers, including the turn-order roll and the first board turns.

```
$ tools/netplay/hashdiff.py h.cut g.cut
identical: 41179 frames
overlays: -1@0, 89@2, -1@15377, 3@15378
```

Both `partyboard` logs had the same netplay lines (only the seat differs):

```
netplay: lobby started (epoch 0): seed 18E274D0F504B413, this machine plays seats 1P   (guest: 2P)
netplay: session start, seed EDE6C0C3
netplay: match start: board 0, 20 turns, bonus 1, teams 0
netplay: match state set at frame 0, entering board 0
netplay: match start: killed 1 menu processes
netplay: heap 0: 8 used blocks, 84992 bytes, layout 6B20845103284ADA
netplay: board overlay 89 starts at frame 1, turn 1/20, heaps 1B85A949F5456D7F
```

Neither log has a `desync` line. The match went title screen → board (ovl 89, Toad's Midway Madness) at frame 2.
It then played the intro dialog, the turn-order roll and the first turn of all four players (2 humans, 2 CPUs),
for 15375 board frames. After that the first minigame's explanation screen (ovl 3 = instDll, "Order Up") ran
from frame 15378 until I killed both processes. Both screens showed the same picture at every screenshot.

**Heap layout was identical too**, on every one of the 41179 frames (hashdiff prints a `note:` when `heap=`
differs, and there was none; the heap hash took 134 different values). N26's lobby run had the heap layouts
diverge from board frame 4 (filed as N45). This run did not reproduce that. Differences from N26's run: the
pads were not idle, it is a later HEAD, and the windows ran in a nested X server instead of rootful Xwayland on
the desktop. I did not dig further: that is N45's job, but this is a data point for it.

## How the run was made

- Build: `cmake --preset linux-default-relwithdebinfo && cmake --build --preset linux-default-relwithdebinfo`.
- Each peer had its own user dir under `build/n35run/{host,guest}` (`XDG_DATA_HOME`, `HOME`). It had a
  `config.json` with `"video.enableVsync": false` and an `online.json` with
  `{"relay":"off","local":1,"players":[{"name":"host"}]}` (or `"guest"`). Disc: `PB_DISC`.
- Display: gamescope headless (`gamescope --backend headless -w 2560 -h 960 -W 2560 -H 960 -- sleep ...`) as a
  Wayland compositor, and inside it a rootful `WAYLAND_DISPLAY=gamescope-0 Xwayland :98 -geometry 2560x960`.
  Both games ran on `DISPLAY=:98 SDL_VIDEODRIVER=x11` (`env -u WAYLAND_DISPLAY`), side by side at 1280x960
  each (`xdotool windowmove`). xdotool, the clipboard and `import -window root` stay inside :98. Nothing
  appeared on the desktop.
- Command per peer: `partyboard --hash-log <peer>.hash` (no script).
- Lobby: Play → F1 → Online. Host: Host Game → Copy Long Code. Guest: Join → Paste Code → Join Game → (mouse
  wheel scroll) Ready. Host: scroll → Start Match. Mouse clicks on the RmlUi controls worked for every button,
  including the Ready SelectButton.
- Input: about 15 s into the match (the board was waiting on the host's A at Toad's welcome text), on each
  peer: F1 → Settings → Input → Configure Controller → Port 1 Controller = Keyboard, Buttons A=J, B=K,
  START=L. Then a loop alternately focused each window and tapped J (every 17th tap K), about every 1.5 s
  per peer, for 7 minutes. Keys must be held: `xdotool keydown j; sleep 0.15; xdotool keyup j`. A bare
  `xdotool key j` was not seen by the binding capture.
- Logs: the hash log restarts at frame 0 at the match (`netplay_begin`), so each log was cut at its last
  `^0 ` line, and then both were cut to the shorter length. The host was 176 lines ahead when killed.

## Tried and failed

- Rootful `Xwayland :97` straight on the desktop (N26's way): Hyprland tiled its window to 586x643, so the
  1280x960 game window was mostly off-screen. Nesting Xwayland in gamescope headless gave a real 2560x960
  screen.
- Running the games directly on gamescope's own rootless Xwayland (`:1`): both processes quit at the title
  screen (`OSResetSystem is a stub`, then exit). I did not investigate. The nested rootful Xwayland works.
- `xdotool key <k>` (press and release with no delay) for the key binding capture: ignored. Hold the key.

## Not verified

- **The minigame itself**: the run stopped at instDll's explanation screen. My loop sent A/B only, and instDll
  waits for START. The minigame, its result and the return to the board were not played in this run.
- **Board turns past the first round**: 15375 board frames were played (intro, order roll, one turn each).
  Later turns, items, shops and star purchases were not reached.
- **Stick input**: no stick or D-pad was bound. Junctions and menu cursor moves used only the default choice (A).
- **Relay / short code**: relay off and the long code on one machine, as in N26. The short code (FJ3V) was
  shown but not used.
- **Two machines / a real network**: both peers ran on localhost.
- **Other platforms**: Linux only. There are no Windows/Apple toolchains here.
- **Why gamescope's rootless Xwayland made the game exit at the title**: not looked at. It is a test-harness
  issue, not netplay.
