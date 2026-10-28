# N93: the web build in more browsers (Firefox, WebKit, a phone-sized Chromium), saves, touch, gamepad

N87 only ran headless Chromium 154. No phone, Mac, Windows box or real gamepad is attached to this machine, so
everything here is **Playwright 1.64.1 on Linux** (AMD RDNA4): Chromium 154 (`/usr/bin/chromium`, the GPU flags of
`tools/web_check.mjs`), Firefox 157 (Playwright's build, `dom.webgpu.enabled`), WebKit 27.2 (Playwright's WPE
MiniBrowser, the closest to Safari there is on Linux). Real devices are follow-up **N95**.

The earlier attempt (wip/N93, a session that ended before committing) left the page changes, the default keyboard
and `tools/web_pw.mjs`. They are taken over here and checked below.

## What changed

- **Firefox: GX shaders naga accepts** (`cmake/patches/aurora/0005-gx-firefox-wgsl.patch`). Firefox stopped on the
  first shader (`unreachable` after `Shader module creation failed`). Chromium's Tint allows both of these, Firefox's
  naga does not:
  - `array_start: array<u32, 12>` in the uniform struct: "The array stride 4 is not a multiple of the required
    alignment 16" (Chromium has the WGSL `uniform_buffer_standard_layout` feature). It is now `array<vec4u, 3>`,
    indexed `[i / 4][i % 4]`. The bytes are the same (48 at offset 32, `shader_info.cpp`'s size is unchanged), and the
    change applies on every platform because it is plain WGSL.
  - `fn load_word(p: ptr<storage, array<u32>>, ...)` and the ~40 helpers built on it: "a pointer of space Storage
    ... can't be passed into functions" (no `unrestricted_pointer_parameters` in naga). On Emscripten only,
    `build_shader` runs `without_storage_ptr_params` on the source. It writes each such function twice, as
    `f__vbuf(...)` and `f__abuf(...)` with `p[`/`arrayLength(p)`/`(p, ` on the global buffer, and rewrites every
    `f(&vbuf, ` / `f(&abuf, ` call (whole-identifier matches only: `fetch_u8_1(` is not `raw_fetch_u8_1(`). The
    desktop's shader source is unchanged apart from `array_start`.
- **Page (`platforms/web/pb.js`, `index.html`)**, from wip/N93:
  - A WebGPU check at load (`navigator.gpu` and `requestAdapter()`). On failure it shows a message with a hint per
    browser (Safari 26 / Safari 18 feature flag, Firefox on Windows 141+ / macOS 145+ / about:config elsewhere,
    Chrome on Android 12+, Chrome on Linux flags), hides the picker and disables Play; a stored disc can still be
    forgotten. There is **still no WebGL2 path**: aurora draws only with WebGPU, and Dawn has no GL backend in the
    browser. Writing one would be a second renderer, out of scope here.
  - OPFS without `createWritable` (Safari before 26): the disc is written from a worker with
    `createSyncAccessHandle` (Safari 15.2+), in 8 MB pieces with progress. When nothing can write it (no OPFS, quota
    exceeded, a failed write), the partial copy is removed and the disc is used **for this visit only** (memory). The
    page also asks for `navigator.storage.persist()`. `?opfs=worker` forces the worker path for tests.
  - User dir (`/libsdl`, IDBFS): saved 200 ms after the game writes, renames or deletes anything under it (FS
    calls wrapped), every 30 s, on `pagehide`, and when the page is hidden (mobile browsers can kill a background tab
    without `pagehide`). Only one `syncfs` runs at a time. The earlier session found that a save on `pagehide` alone
    lost a file written just before a reload; not re-checked here (see Not verified).
  - Errors: Safari's `Error.stack` has no message, so errors are shown as `name: message`. Reading a disc too big
    for memory says so and suggests a compressed image.
- **Default keyboard (`src/port/portmain.cpp`, `__EMSCRIPTEN__` only):** with no `keyboard_bindings.dat`, port 1
  gets arrows = stick, X/Z/C/S = A/B/X/Y, Enter = Start, Q/W/E = L/R/Z, I/K/J/L = D-pad. N87 found Enter at the title
  did nothing until the keyboard was bound in Settings, and a browser player usually has no pad. Binding keys in
  Settings writes the file, which replaces this. Not in `--script` runs (added here: the hash logs stay comparable
  with N87's and the desktop's).
- **`tools/web_pw.mjs`** (from wip/N93, plus a `pick:` step): Playwright driver for chromium/firefox/webkit with a
  persistent profile, keyboard/touch/drag/gamepad/rotation/reload/eval/dump/memory steps. Needs the `playwright`
  package (`npm install playwright` anywhere, `PLAYWRIGHT=<dir>/node_modules/playwright`), `PW_EXE=/usr/bin/chromium`
  for Chromium. On Arch, Playwright's WebKit needs Ubuntu 24.04's `libicu74`, `libxml2` (.so.2), `libflite1`,
  `libhwy1t64` from archive.ubuntu.com unpacked into a *copy* of `~/.cache/ms-playwright/webkit-2370/minibrowser-wpe/sys/lib`
  (its launcher resets `LD_LIBRARY_PATH`). `libjxl.so.0.8` is a symlink to the system's 0.12, which was enough to
  start it. Then run with `PW_EXE=<copy>/pw_run.sh PLAYWRIGHT_SKIP_VALIDATE_HOST_REQUIREMENTS=1`. Put `PW_PROFILE`
  on a real disk: each profile holds the 600 MB disc in OPFS (the session's tmpfs quota filled after ~10 runs).

## What works, per browser

| | Chromium 154 | Firefox 157 (Linux, pref on) | WebKit 27.2 (WPE) |
|---|---|---|---|
| WebGPU adapter | yes | yes | no `navigator.gpu` |
| Boots, title, menus | yes | yes, after 0005 (`01-firefox-title-to-party.jpg`) | the no-WebGPU message (`02-webkit-no-webgpu.jpg`) |
| Scripted party (`party.script --match 0@1500 --turbo`, 200 s) | board w01, turn 1 | board w01, turn 1 | - |
| Per-frame hashes | = Firefox for 5488 frames, all fields incl. `heap=`; = desktop Linux build for 5491 | = Chromium | - |
| OPFS `createWritable` | yes | yes | yes |
| OPFS worker path (`?opfs=worker`, 20 MB file written, read back byte-checked) | yes | yes | yes |
| Memory-only fallback (OPFS write made to fail, real disc) | boots; nothing offered after reload | not run | - |
| Keyboard default mapping (title -> memory card -> File 1 -> Party Mode) | yes | yes | - |
| Save kept across a browser restart (File 1 created, browser closed, profile reopened: File 1 has data, `05-save-after-restart.jpg`) | yes | not run | - |
| Gamepad (emulated standard-mapping pad through `navigator.getGamepads`; START, START, A to file select, prompts switch to pad icons) | yes | not run | - |
| Touch pad (N70) through SDL's Emscripten touch events, phone viewport 915x412 @2.625 with mobile UA | shown on first tap; START and A on it drive the menus | - | - |
| Rotation (412x915 <-> 915x412) | canvas follows at device pixels (2402x1082), pad re-laid out, game renders wider/narrower as on other platforms | - | - |

Screenshots: `03-phone-touch-rotation.jpg` (touch: memory card -> A -> file select, then portrait),
`04-touch-and-gamepad.jpg` (top: touch run, bottom: gamepad run).

Memory with the whole 600 MB `.ciso` in memory: Chromium's `performance.memory.usedJSHeapSize` is ~634 MB right after
boot and stays there (it counts the disc's ArrayBuffer). The wasm heap is on top of that. On a phone that is the
risk, and it was not measured on one.

Speed (rough: the machine's load average was ~34 from other agents): 90 s of the turbo party gave 5259 frames
through `web_check.mjs` and 3817 through `web_pw.mjs`, both Chromium alone. Two browsers at once each did ~5490 in
200 s. Playwright's Chromium launch flags are probably part of the difference; not looked into.

## Found on the way

- **The game's clock is an hour behind during daylight saving time on every platform**, not only the web. A
  save made at 03:03 EDT shows 02:03. aurora's `OSGetTime` takes the UTC offset as
  `mktime(localtime) - mktime(gmtime)`, and `gmtime`'s `tm_isdst = 0` makes `mktime` read it as standard time: -5 h
  in EDT. The same C on the desktop's glibc prints -5 h (`tm_gmtoff` says -4). Not fixed here (shared aurora code,
  OSGetTime feeds netplay): **N94**.

## Tried and dropped

- WebKit with the system's libraries: Playwright's host check refuses Arch, and its launcher resets
  `LD_LIBRARY_PATH`. Hence the copied bundle above. A WebKit with WebGPU was not found: WPE 27.2 has none switched
  on, and Playwright can't set WebKit feature flags.

## Checks

- `tools/check.sh` passes. The desktop build with 0005 runs the scripted party for 61016 frames with no
  shader/validation error, and its hashes equal Chromium's for the 5491 frames compared.
- 0005 applies to a pristine `git show HEAD:lib/gx/shader.cpp` of the submodule, and reverse-applies to the patched
  tree (what `aurora_patches.cmake` checks).

## Not verified

- **Real devices and the browsers the brief names:** Safari 26 on iOS and macOS (WebGPU, OPFS, `createWritable`,
  touch, memory on an iPhone), Chrome on an Android phone (real touch, rotation, memory limits with a whole disc in
  memory), Firefox on Windows/macOS (where WebGPU ships; here it was Linux with the pref on). There are no such
  devices on this machine. Playwright's engines on Linux stand in for them (above). **N95**.
- **Safari before 26's real OPFS:** the worker path ran in Chromium, Firefox and WebKit 27.2 (which all also have
  `createWritable`), not in a Safari without it.
- **A WebGL2 path:** none. The message stays (with per-browser hints). aurora has only a WebGPU renderer.
- **Visual correctness of the desktop's indexed vertex arrays after `array_start` became `vec4u`:** the desktop run
  checked hashes and the absence of errors, not pictures. The same generated expressions rendered the title, menus and
  board correctly in both browsers.
- **The touch stick:** a drag on the floating stick gave no visible change on the file-select screen; not followed
  up (N70 checked the stick on the desktop through SDL finger events).
- **Firefox and WebKit: saves across restarts, the gamepad, touch:** only Chromium ran those.
- **The IDBFS save timing:** saves survived a full browser close in Chromium. The earlier session's claim that
  `pagehide` alone lost writes was not re-tested against the old page.
- **A real gamepad:** only an emulated `navigator.getGamepads()` pad. Real pads' mappings (non-standard ones) not
  tried.
- **Audio:** stubbed in every build.
