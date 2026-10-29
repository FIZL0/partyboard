// N87: Party Board's page. The player picks their disc image; it is kept in the origin's private storage (OPFS) for
// the next visit, read into memory and handed to the game as a file in Emscripten's in-memory FS (PB_DISC). The
// game's user dir (config, memory card saves) is SDL's /libsdl, kept in IndexedDB (IDBFS) and saved there 200 ms after
// the game writes to it.
//
// Query: ?args=... passes command-line arguments to the game (space-separated, e.g. "--script /res/x --turbo");
// ?autoplay=1 starts with the stored disc without a click; ?fetch=a,b puts test/a and test/b (from the server) in
// the game's /test (tools/web_check.mjs); ?env=K=V,K2 sets environment variables for the game; ?opfs=worker stores the
// disc as browsers without createWritable do (tools/web_pw.mjs); ?log=1 shows the console on the page.
// N98: ?code=rtco... (N139: or a short code, ?code=K7MQ) is a join link (the host's Online lobby copies one): the
// game starts with the stored disc (or once one is picked) and joins that lobby.
'use strict';
const $ = id => document.getElementById(id);
const query = new URLSearchParams(location.search);
const joinCode = query.get('code');
const status = text => { $('status').textContent = text; };
const fail = text => { $('error').textContent += text + '\n'; console.error(text); };
// Safari's Error.stack has no message in it. A wasm trap's stack (wasm-function[N] frames) goes along: the release
// wasm has no names, N99's recipe names them.
const describe = e => (e instanceof Error
  ? `${e.name}: ${e.message}` + (e instanceof WebAssembly.RuntimeError && e.stack ? `\n${e.stack}` : '')
  : String(e));
const DISC_DIR = 'disc';

// ?log=1: the console (the game's log included) on screen, for browsers without a reachable inspector (iOS Safari
// without a Mac). The last 300 lines.
if (query.get('log')) {
  const box = $('log'), lines = [];
  box.hidden = false;
  for (const level of ['log', 'info', 'warn', 'error']) {
    const orig = console[level].bind(console);
    console[level] = (...a) => {
      orig(...a);
      lines.push((level === 'log' || level === 'info' ? '' : level + ': ') + a.map(String).join(' '));
      if (lines.length > 300) lines.shift();
      box.textContent = lines.join('\n');
      box.scrollTop = box.scrollHeight;
    };
  }
}

fetch('version.txt').then(r => r.ok ? r.text() : '').then(v => { $('build').textContent = v.trim(); }, () => {});

async function discDir() {
  if (!navigator.storage?.getDirectory) return null;
  try {
    return await (await navigator.storage.getDirectory()).getDirectoryHandle(DISC_DIR, { create: true });
  } catch (e) {
    console.warn('no OPFS:', e);
    return null;
  }
}

// The stored disc (a File), if any.
async function storedDisc() {
  const dir = await discDir();
  if (!dir) return null;
  for await (const [, handle] of dir.entries()) {
    if (handle.kind === 'file') return handle.getFile();
  }
  return null;
}

async function forgetDisc() {
  const dir = await discDir();
  if (!dir) return;
  for await (const [name] of dir.entries()) await dir.removeEntry(name);
}

// N93: where the page can't write OPFS (no createWritable: Safari before 26), a worker can, with a sync access handle
// (Safari 15.2+).
const STORE_WORKER = `onmessage = async ({ data: [dirName, file] }) => {
  let handle;
  try {
    const dir = await (await navigator.storage.getDirectory()).getDirectoryHandle(dirName, { create: true });
    handle = await (await dir.getFileHandle(file.name, { create: true })).createSyncAccessHandle();
    handle.truncate(0);
    const piece = 8 << 20;
    for (let at = 0; at < file.size; at += piece) {
      handle.write(new Uint8Array(await file.slice(at, at + piece).arrayBuffer()), { at });
      postMessage({ at: Math.min(at + piece, file.size) });
    }
    handle.flush();
    postMessage({ done: true });
  } catch (e) {
    postMessage({ error: e.name + ': ' + e.message });
  } finally {
    handle?.close();
  }
};`;

function storeWithWorker(file) {
  return new Promise((resolve, reject) => {
    const url = URL.createObjectURL(new Blob([STORE_WORKER], { type: 'text/javascript' }));
    const worker = new Worker(url);
    URL.revokeObjectURL(url);
    worker.onmessage = ({ data }) => {
      if (data.at) status(`Saving ${file.name} in this browser... ${Math.floor(data.at * 100 / file.size)}%`);
      if (data.done || data.error) worker.terminate();
      if (data.done) resolve();
      if (data.error) reject(new Error(data.error));
    };
    worker.onerror = e => { worker.terminate(); reject(new Error(e.message || 'storage worker failed')); };
    worker.postMessage([DISC_DIR, file]);
  });
}

// Keeps `file` for the next visit; returns false where the browser can't keep it in OPFS (no OPFS, too little
// quota: it's then used for this visit only).
async function storeDisc(file) {
  const dir = await discDir();
  if (!dir) return false;
  await forgetDisc();
  status(`Saving ${file.name} in this browser...`);
  // persistent storage: without it the browser may clear the disc and saves under storage pressure (and Safari
  // after 7 days without a visit); granted or not, nothing changes otherwise
  navigator.storage.persist?.().then(p => console.log(`persistent storage: ${p}`), () => {});
  try {
    const handle = await dir.getFileHandle(file.name, { create: true });
    if (handle.createWritable && query.get('opfs') !== 'worker') {
      await file.stream().pipeTo(await handle.createWritable());
    } else if (typeof Worker !== 'undefined') {
      await storeWithWorker(file);
    } else {
      throw new Error('no way to write OPFS here');
    }
    return true;
  } catch (e) {
    // a partial copy would be offered next time
    await forgetDisc().catch(() => {});
    throw e;
  }
}

// What keeps the game from drawing here, or null. aurora renders with WebGPU only (no WebGL path).
let gpuWarning = null;
async function webgpuProblem() {
  const ua = navigator.userAgent;
  const hint = /iPhone|iPad|Macintosh/.test(ua) && !/Chrome|Firefox|FxiOS|CriOS/.test(ua)
    ? 'Safari has WebGPU from Safari 26 (iOS/iPadOS/macOS 26). On Safari 18 it is under Settings > Apps > Safari > ' +
      'Advanced > Feature Flags > WebGPU (macOS: Develop > Feature Flags).'
    : /Firefox/.test(ua)
      ? 'Firefox has WebGPU on Windows (Firefox 141+) and macOS on Apple silicon (145+). Elsewhere (Linux, Android) ' +
        'it is behind dom.webgpu.enabled in about:config.'
      : /Android/.test(ua)
        ? 'Chrome on Android has WebGPU from Chrome 121 on Android 12+ with a supported GPU; ' +
          'chrome://flags/#enable-unsafe-webgpu turns it on for others.'
        : 'Chrome and Edge 113+ have WebGPU on Windows, macOS and ChromeOS. On Linux turn on ' +
          'chrome://flags/#enable-unsafe-webgpu and chrome://flags/#enable-vulkan.';
  if (!navigator.gpu) return `This browser has no WebGPU, which Party Board draws with. ${hint}`;
  try {
    const adapter = await navigator.gpu.requestAdapter();
    if (!adapter) {
      return `This browser has WebGPU but offers no graphics adapter (the GPU or its driver may be blocklisted). ${hint}`;
    }
    // A software adapter (Chrome's SwiftShader, e.g. Linux without the flags) plays, but at a few frames a second.
    const info = adapter.info ?? {};
    if (info.isFallbackAdapter || adapter.isFallbackAdapter
        || /swiftshader/i.test(`${info.vendor} ${info.architecture} ${info.description}`)) {
      gpuWarning = `WebGPU is running in software here, not on your GPU, so the game will be very slow. ${hint}`;
    }
  } catch (e) {
    return `WebGPU failed: ${describe(e)}. ${hint}`;
  }
  return null;
}

let pending = null;
async function refresh() {
  const disc = pending ?? await storedDisc();
  $('play').hidden = !disc;
  $('forget').hidden = !disc || disc === pending;
  status(disc ? `Disc: ${disc.name} (${(disc.size / 1048576).toFixed(0)} MB)` : 'No disc image chosen yet.');
  return disc;
}

function loadScript(src) {
  return new Promise((resolve, reject) => {
    const s = document.createElement('script');
    s.src = src;
    s.onload = resolve;
    s.onerror = () => reject(new Error(`could not load ${src}`));
    document.head.appendChild(s);
  });
}

// N93: the user dir goes to IndexedDB shortly after the game changes it. A save on pagehide alone doesn't land: the
// page is gone before IndexedDB's transaction commits (a file written just before a reload was lost every time).
function watchUserDir(FS) {
  let syncing = false, again = false, timer = 0;
  const save = () => {
    clearTimeout(timer);
    if (syncing) {
      again = true; // one syncfs at a time
      return;
    }
    syncing = true;
    FS.syncfs(false, err => {
      syncing = false;
      if (err) console.warn('IDBFS save:', err);
      if (again) {
        again = false;
        save();
      }
    });
  };
  const ours = a => (typeof a === 'string' ? a : a?.path ?? '').startsWith('/libsdl/');
  for (const name of ['write', 'rename', 'unlink', 'rmdir', 'mkdir', 'truncate', 'ftruncate', 'symlink', 'utime']) {
    const real = FS[name];
    FS[name] = (...args) => {
      const result = real(...args);
      // ftruncate has only an fd
      if (name === 'ftruncate' || args.some(ours)) {
        clearTimeout(timer);
        timer = setTimeout(save, 200);
      }
      return result;
    };
  }
  setInterval(save, 30000);
  addEventListener('pagehide', save);
  // mobile browsers may kill a page in the background without pagehide
  addEventListener('visibilitychange', () => { if (document.visibilityState === 'hidden') save(); });
}

let started = false;
async function play(disc) {
  if (started) return;
  started = true;
  try {
    const problem = await gpuCheck;
    if (problem) throw new Error(problem);
    status(`Reading ${disc.name}...`);
    let bytes;
    try {
      // the whole image: the game reads it synchronously, which a File can't be from the page
      bytes = new Uint8Array(await disc.arrayBuffer());
    } catch (e) {
      throw new Error(`Could not read ${disc.name} (${(disc.size / 1048576).toFixed(0)} MB) into memory: ${describe(e)}. ` +
        'The whole image is held in memory; a compressed one (.rvz or .ciso) needs less.');
    }
    const fetched = [];
    for (const name of (query.get('fetch') ?? '').split(',').filter(Boolean)) {
      const r = await fetch(`test/${name}`);
      if (!r.ok) throw new Error(`test/${name}: ${r.status}`);
      fetched.push([name, new Uint8Array(await r.arrayBuffer())]);
    }
    status('Starting...');
    await loadScript('partyboard.js');
    const args = (query.get('args') ?? '').split(' ').filter(Boolean);
    const canvas = $('canvas');
    document.body.classList.add('playing');
    window.pbModule = await createPartyBoard({
      canvas,
      arguments: args,
      print: text => console.log(text),
      printErr: text => console.warn(text),
      preRun: [module => {
        const FS = module.FS;
        FS.mkdir('/disc');
        // canOwn: the FS keeps this array as the file's contents instead of copying a disc's worth of bytes
        FS.createDataFile('/disc', disc.name, bytes, true, false, true);
        module.ENV.PB_DISC = `/disc/${disc.name}`;
        if (joinCode) module.ENV.PB_JOIN_CODE = joinCode;
        for (const kv of (query.get('env') ?? '').split(',').filter(Boolean)) {
          const at = kv.indexOf('=');
          module.ENV[at < 0 ? kv : kv.slice(0, at)] = at < 0 ? '1' : kv.slice(at + 1);
        }
        if (fetched.length) FS.mkdir('/test');
        for (const [name, data] of fetched) FS.writeFile(`/test/${name}`, data);
        FS.mkdir('/libsdl');
        FS.mount(module.IDBFS, {}, '/libsdl');
        module.addRunDependency('idbfs');
        FS.syncfs(true, err => {
          if (err) console.warn('IDBFS load:', err);
          module.removeRunDependency('idbfs');
        });
      }],
      onAbort: what => fail(`The game stopped: ${what}`),
    });
    canvas.focus();
    watchUserDir(window.pbModule.FS);
    // a reload mustn't join again with the same code (the host's offer in it is spent once used)
    if (joinCode) {
      const url = new URL(location.href);
      url.searchParams.delete('code');
      history.replaceState(null, '', url.href);
    }
  } catch (e) {
    document.body.classList.remove('playing');
    fail(describe(e));
    if (e?.stack) console.error(e.stack);
    started = false;
  }
}

$('pick').addEventListener('change', async () => {
  const file = $('pick').files[0];
  if (!file) return;
  try {
    pending = (await storeDisc(file)) ? null : file;
  } catch (e) {
    console.warn(`could not keep the disc: ${describe(e)}`);
    pending = file;
  }
  const disc = await refresh();
  if (disc) play(disc);
});
$('play').addEventListener('click', async () => { const disc = await refresh(); if (disc) play(disc); });
$('forget').addEventListener('click', async () => { await forgetDisc(); refresh(); });
// N92: SDL has no clipboard in the browser (src/port/web/web_clipboard.c): Ctrl+V on the page hands the clipboard's
// text to the game, whose Paste buttons (Join code, a guest's answer) then use it. SDL takes the key itself (no
// paste event), so the text is read with the async clipboard API (the browser may ask for permission once).
const pasteToGame = text => {
  const m = window.pbModule;
  if (!text || !m?._pb_web_paste) return;
  const p = m.stringToNewUTF8(text);
  m._pb_web_paste(p);
  m._free(p);
};
addEventListener('keydown', e => {
  if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'v' && navigator.clipboard?.readText) {
    navigator.clipboard.readText().then(pasteToGame, err => console.warn('clipboard:', err));
  }
}, true);
addEventListener('paste', e => pasteToGame(e.clipboardData?.getData('text')));
addEventListener('error', e => fail(e.error ? describe(e.error) : e.message));
addEventListener('unhandledrejection', e => fail(describe(e.reason)));

const gpuCheck = webgpuProblem();
gpuCheck.then(problem => {
  if (gpuWarning) $('error').textContent += gpuWarning + '\n';
  if (!problem) return;
  // nothing to pick a disc for; a stored one can still be forgotten
  fail(problem);
  $('pick-label').hidden = true;
  $('play').disabled = true;
});
refresh().then(disc => {
  if (disc && (query.get('autoplay') || joinCode)) play(disc);
  else if (joinCode) status('Pick your disc image to join the game.');
});
