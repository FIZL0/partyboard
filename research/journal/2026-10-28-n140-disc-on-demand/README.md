# N140: the browser reads the disc on demand

User 2026-10-28: iOS Safari (iPhone 15 Pro) killed the tab ("A problem repeatedly occurred", before that
`RuntimeError: unreachable` with no log lines). The page held the whole image in memory: `disc.arrayBuffer()` in
`platforms/web/pb.js` handed to MEMFS. Now the game's disc file in Emscripten's FS is a node whose `read` reads just
that byte range from the stored disc (the OPFS file, or the picked File).

## Why not Asyncify (or a Worker)

The game's disc reads go game -> aurora `DVDRead` -> `nod_read` (Rust) -> nod's `FfiDiscStream` -> aurora's
`sdlStreamReadAt` (C) -> SDL -> stdio -> Emscripten's `fd_read`/`fd_pread` -> the FS node. For an Asyncify suspend in
the node, every frame from there up needs instrumenting, but the nod frames have wasm exception handling (Rust std is
built with `-fwasm-exceptions`): `build/web-cmake/web_asyncify_remove.txt` (tools/web_eh_list.py, the list Asyncify
must skip) has `nod_read`, `nod_seek`, `nod_buf_read`, `nod::stream::FfiDiscStream::lock` and 220 more nod functions.
Asyncify can't unwind through them, so a read can't wait for a promise there. (nod built with `panic=abort` could drop
most landing pads, but std's precompiled functions keep theirs, and the stable toolchain can't rebuild std.)

The brief's fallback, running the game in a Worker with an OPFS `FileSystemSyncAccessHandle`, would move SDL, the
WebGPU canvas (OffscreenCanvas), input, audio, IDBFS and the clipboard/WebRTC glue off the page's thread: a large
rework of the port for this one read. Not done.

Instead, a **synchronous XHR of a blob URL** of `file.slice(a, b)` works on the page's thread in all three engines.
A synchronous XHR on the page can't have `responseType = 'arraybuffer'`, so it is read as text with
`overrideMimeType('text/plain; charset=x-user-defined')`: one char per byte (0x80-0xFF come back as U+F780-F7FF; the
low byte is the byte). This is the old trick Emscripten's own `createLazyFile` uses in workers. No change to the
wasm, C, Rust, cmake or tools/web.sh: the brief's `cmake/web.cmake` / `src/port` / ASYNCIFY_ONLY changes are not needed.

`pb.js`:
- `discBlockReader(file)`: 1 MB blocks, the 16 most recently used kept (16 MB), each fetched by a sync XHR of
  `URL.createObjectURL(file.slice(...))` (revoked right after; `xhr.abort()` drops the browser's copy of the text).
  Reads copy straight into the destination (Emscripten passes HEAP8; copied through a Uint8Array view: `set()`
  between element types converts each element).
- `createReaderFile(FS, ...)`: a MEMFS file node (stat/seek: `usedBytes` = the File's size) with its own
  `stream_ops` (`read` -> the reader, errors -> EIO; write EROFS; mmap ENODEV), the way `createLazyFile` does it.
- `play()` probes a 32-byte read first; if the sync read fails (a browser without it), it falls back to the old
  whole-image read and logs why. `?disc=memory` forces the old path (comparisons). The console says which:
  `disc: NAME, SIZE bytes, read on demand|in memory`.
- Picker, OPFS storage (createWritable and the worker path) and Forget disc are unchanged; the File handed to the
  reader is the OPFS file's `getFile()` or the picked File.

## Results

Builds: `build/web-mem` = tools/web.sh at 0195715b (the old page); `build/web-ond` = the same files with the new
pb.js (the wasm is byte-identical: only the page changed). GMPE01 Rev 1 `.ciso`, 599818816 bytes (572 MiB).

**Renderer memory at the title** (`hwm.sh`: tools/web_check.mjs, VmHWM/VmRSS of the `--type=renderer` process,
sampled every second; Chromium 154 on the GPU):

| run | peak VmHWM | RSS at the title (steady) | JS heap (performance.memory) | wasm memory |
|---|---|---|---|---|
| in memory, disc picked (web_check's default) | 1048388 kB | ~1000 MB | 600 MB | 368 MB |
| on demand, disc picked | 619824 kB | ~435 MB after the first full GC (~30 s), 553 MB before | 45 MB (94-110 MB before that GC) | 368 MB |
| in memory, stored disc (`DISC=-`, autoplay) | 1056572 kB | ~1010 MB | | |
| on demand, stored disc | 634868 / 618044 kB | ~440 MB | | |
| on demand, 1.4 GB `.iso` (ciso2iso.py), picked | 526308 kB | | | |

So RSS at the title drops by ~565 MB = the disc's size, and the JS heap by 555 MB. The peak (VmHWM) drops by ~430 MB:
the boot reads 53 blocks (53 MB, 192 ms of XHR in all) in under a second, and their text (2 bytes a char) is garbage
until V8's next full GC ~2 s later: the peak is ~180 MB above steady for that moment (the in-memory build's ~46).
Smaller blocks don't help (256 KB: 621 MB peak, 173 XHRs, 171 ms; 64 KB: 600 MB, 653 XHRs, 371 ms) and `xhr.abort()`
took only ~3 MB off; kept 1 MB. The `.iso` peak is lower than the `.ciso`'s: probably nod's CISO reader buffers more
(not looked into).

**Hash log**, party.script `--match 0@1500 --turbo --hash-log`, 240 s each (`party.sh`), one after the other: in
memory 40545 frames, on demand 40068; `tools/netplay/hashdiff.py` on the first 40068: **identical, heap= included**
(the brief asks for 13500), through w01's first minigames (ovl 23, 42, 51, 62). About the same speed (40068 vs 40545 frames in
the same time is within run-to-run noise).

**Browsers** (tools/web_pw.mjs; `readcheck.js` reads the stored disc with `discBlockReader` at 207 offsets, block
boundaries, spans, the end and past it, and compares with `File.slice().arrayBuffer()`):

| | readcheck | game |
|---|---|---|
| WebKit 27.2 (Playwright WPE) | 207 reads, 0 bad (46 MB in 608 ms with the async reads) | no `navigator.gpu` here (as N93): the page shows the no-WebGPU message, the game can't start |
| Firefox 157 | 0 bad | boots to the title, `read on demand` |
| Chromium 154 | 0 bad | boots to the title (web_check) |

`?disc=memory` on the new page: `in memory`, title, 1051860 kB peak as before.

WebKit setup (N93's patched copy was gone): `npm install playwright` in `.n140-runs/pw`, a copy of
`~/.cache/ms-playwright/webkit-2370` with Ubuntu 24.04's `libicu74_74.2-1ubuntu3.1`, `libxml2_2.9.14+dfsg-1.3ubuntu3.10`
and `libflite1_2.2-6build3` unpacked into `minibrowser-wpe/sys/lib`; then `PW_EXE=<copy>/pw_run.sh
PLAYWRIGHT_SKIP_VALIDATE_HOST_REQUIREMENTS=1 PLAYWRIGHT=.n140-runs/pw/node_modules/playwright`.

## Files here

- `hwm.sh OUT SECS [QUERY]`: web_check + renderer VmHWM/RSS (RssAnon/File/Shmem) samples (`HWM_EVERY`, default 1 s).
- `memlog.js`: `WEB_PRELOAD` that logs JS heap, wasm memory, blob XHR count and time every 5 s.
- `party.sh WEB_ROOT OUT SECS`: the scripted party with a hash log.
- `readcheck.js`: the reader against File.slice, for `web_pw.mjs ... PW_STEPS='25:evalfile:readcheck.js'`.
- `ciso2iso.py`: a .ciso as a plain .iso.

## Not verified

- **iOS Safari on a device**, the case that started this: no iPhone or Mac here. WebKit on Linux (WPE) shows that the
  synchronous blob XHR reads right, but it has no WebGPU, so the game never ran on WebKit; whether the iPhone tab now
  stays alive (and whether the `unreachable` was the disc's memory at all) needs the user's phone. Its wasm memory
  (368 MB) and the transient ~100 MB of read garbage at boot remain.
- **`.rvz`**: no RVZ image or converter (dolphin-tool, nodtool) here. The reader is format-blind (byte ranges); nod's
  RVZ decoding above it is unchanged. The `.iso` (converted from the .ciso) and `.ciso` were run.
- **Chrome on Android**: not run. Synchronous XHR on the page's thread is deprecated (Firefox logs a warning once;
  Chromium's web_check console showed none) but supported in all three engines; if a browser ever drops it, `play()`
  falls back to the whole image in memory and logs why.
- **Two-peer online play** with the new reader: not run; the disc reads go through the same FS node, and the hash log
  of a whole scripted party segment matched.
