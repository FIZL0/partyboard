# N95: the web build on real devices (blocked: needs a human with the devices)

The brief asks for a human: Safari 26 on iOS/macOS, Chrome on an Android phone, Firefox on Windows/macOS and a
real gamepad. This session ran unattended on the Linux box, and none of those is here:

- `lsusb` / `/dev/input/by-id`: keyboard, mouse, audio interface, webcam, LED controller. No gamepad, no phone.
- `~/Android/Sdk/platform-tools/adb devices`: nothing listed. An emulator would not count as a real phone. Its
  Chrome has no WebGPU without host GPU passthrough, and N93's phone-viewport Chromium already covered the same layout.
- No macOS, iOS or Windows machine (see memory note "No Apple/MSVC toolchains").

No code was changed. Below is the checklist for whoever has the devices, so the run takes one sitting.

## Setup

1. `tools/web.sh` builds `build/web/`. OPFS needs a secure context, so serve it over **https** for any other
   device (localhost http is enough only on the build machine). Two ways:
   - `npx http-server build/web -S -C cert.pem -K key.pem` with a cert the phone trusts (mkcert, plus its
     root CA installed on the phone), or
   - a tunnel that gives an https URL (e.g. `tailscale serve`). Don't publish the build anywhere public.
2. Have the US disc (GMPE01) on each device: `.iso` (~1.4 GB) or `.ciso` (~600 MB). Use the `.ciso` on phones.
3. Optional page queries (`platforms/web/pb.js`): `?opfs=worker` forces the sync-access-handle worker path;
   `?autoplay=1` starts with the stored disc.
4. Remote consoles: Safari Web Inspector (Mac + iPhone over USB, "Web Inspector" on in iOS Settings > Safari >
   Advanced); Chrome `chrome://inspect` for Android over USB debugging. The memory figures in the
   tables below need those.

## Checklist (fill in per device)

| Check | How | Safari 26 iOS | Safari 26 macOS | Chrome Android | Firefox Win | Firefox macOS |
|---|---|---|---|---|---|---|
| WebGPU adapter | page loads without the no-WebGPU message | | | | | |
| Disc picked, kept in OPFS | pick disc, progress reaches 100 %, reload: Play offered without picking | | | | | |
| `?opfs=worker` path | same as above with the query (needed for Safari < 26 only; check it works anyway) | | | | | |
| Boots to title, menus | title -> memory card -> File 1 -> Party Mode | | | | | |
| Keyboard default (desktop) | arrows, X/Z = A/B, Enter = Start (N93 mapping) | - | | - | | |
| Touch pad (N70) | appears on first tap; START, A drive the menus; **the floating stick moves the cursor** (N93 saw no change on file select) | | - | | - | - |
| Rotation | portrait <-> landscape: canvas fills, pad re-laid out, no crash | | - | | - | - |
| Plays a board | start a party, play through turn 1 and one minigame | | | | | |
| Save survives | create File 1, close the tab/app fully (swipe away on phones), reopen: File 1 has data | | | | | |
| Background tab | switch apps mid-board for 1 min, come back: still running or reloads cleanly, save kept | | | | | |
| Memory | Safari: Web Inspector Timelines > Memory; Chrome: `chrome://inspect` Memory panel; note peak and whether the tab is killed | | | | | |
| Real gamepad | connect pad (Xbox/PS/Switch Pro), press a button, prompts turn to pad icons, menus driven | | | | | |
| Speed | rough fps on a board (is it playable?) | | | | | |

For the gamepad, also note the pad model and whether the browser reports `mapping: "standard"`
(`navigator.getGamepads()[0].mapping` in the console). A non-standard mapping is the likely failure.

Things that can go wrong, from N93's notes:
- **Memory on phones:** with a 600 MB `.ciso`, the whole disc is an ArrayBuffer (~634 MB JS heap in Chromium) plus the
  wasm heap. iOS Safari may kill the tab. If it does, write down the device model, its RAM and the point where it died.
- **OPFS quota:** a failed write should fall back to "this visit only" (memory). Check that the message shows and
  the game still boots.
- **Safari's error display:** errors show as `name: message`; copy whatever appears.

## Not verified

- **Everything in the brief.** Safari 26 on iOS/macOS, Chrome on an Android phone, Firefox on Windows/macOS and a
  real gamepad: none of these devices is attached to this machine, and the session was unattended, so no human
  was there to run them. The checklist above is for that run.
