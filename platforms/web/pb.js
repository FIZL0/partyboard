// N87: Party Board's page. The player picks their disc image; it is kept in the origin's private storage (OPFS) for
// the next visit, read into memory and handed to the game as a file in Emscripten's in-memory FS (PB_DISC). The
// game's user dir (config, memory card saves) is SDL's /libsdl, kept in IndexedDB (IDBFS).
//
// Query: ?args=... passes command-line arguments to the game (space-separated, e.g. "--script /res/x --turbo");
// ?autoplay=1 starts with the stored disc without a click; ?fetch=a,b puts test/a and test/b (from the server) in
// the game's /test (tools/web_check.mjs); ?env=K=V,K2 sets environment variables for the game.
'use strict';
const $ = id => document.getElementById(id);
const query = new URLSearchParams(location.search);
const status = text => { $('status').textContent = text; };
const fail = text => { $('error').textContent += text + '\n'; console.error(text); };
const DISC_DIR = 'disc';

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

// Keeps `file` for the next visit; returns false where the browser can't write OPFS from the page (it's then
// used for this visit only).
async function storeDisc(file) {
  const dir = await discDir();
  if (!dir) return false;
  await forgetDisc();
  const handle = await dir.getFileHandle(file.name, { create: true });
  if (!handle.createWritable) return false;
  status(`Saving ${file.name} in this browser...`);
  await file.stream().pipeTo(await handle.createWritable());
  return true;
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

let started = false;
async function play(disc) {
  if (started) return;
  started = true;
  try {
    if (!navigator.gpu) {
      throw new Error('This browser has no WebGPU (navigator.gpu), which Party Board draws with. Chrome/Edge 113+, ' +
        'Safari 26+ or Firefox 141+ (Windows) have it; on Linux Chrome needs --enable-unsafe-webgpu.');
    }
    status(`Reading ${disc.name}...`);
    const bytes = new Uint8Array(await disc.arrayBuffer());
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
    const save = () => window.pbModule.FS.syncfs(false, err => err && console.warn('IDBFS save:', err));
    setInterval(save, 5000);
    addEventListener('pagehide', save);
  } catch (e) {
    document.body.classList.remove('playing');
    fail(String(e?.stack ?? e));
    started = false;
  }
}

$('pick').addEventListener('change', async () => {
  const file = $('pick').files[0];
  if (!file) return;
  try {
    pending = (await storeDisc(file)) ? null : file;
  } catch (e) {
    console.warn('could not keep the disc:', e);
    pending = file;
  }
  const disc = await refresh();
  if (disc) play(disc);
});
$('play').addEventListener('click', async () => { const disc = await refresh(); if (disc) play(disc); });
$('forget').addEventListener('click', async () => { await forgetDisc(); refresh(); });
addEventListener('error', e => fail(String(e.error?.stack ?? e.message)));
addEventListener('unhandledrejection', e => fail(String(e.reason?.stack ?? e.reason)));

refresh().then(disc => { if (disc && query.get('autoplay')) play(disc); });
