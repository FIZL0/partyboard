// N87: drives the browser build (tools/web.sh's build/web) in headless Chromium over CDP: serves build/web (and
// WEB_FILES, files the page fetches with ?fetch= into the game's /test), picks DISC in the page's file input,
// screenshots OUT/shot-<s>.png every SHOT_EVERY seconds for SECS seconds and keeps the console in OUT/console.txt
// (Chromium's own log in OUT/chrome.log).
//
// Usage: node tools/web_check.mjs DISC OUT [SECS] [QUERY]
//   DISC  the disc image ('-': OUT/profile already has one in OPFS; QUERY then wants autoplay=1)
//   QUERY the page's query, e.g. 'args=--script /test/party.script --match 0@1500 --turbo&fetch=party.script'
// Env: WEB_ROOT (the build to serve, default build/web), WEB_PORT (8787), CDP_PORT (9339), CHROME (chromium), CHROME_FLAGS (instead of GPU_FLAGS below), SHOT_EVERY (10),
//      WEB_FILES ("a/party.script b/x": served at /test/<basename>), WEB_KEYS ("12:Enter,14:ArrowDown": key presses
//      at those seconds), STOP_ON (a console substring that ends the run early, after one more screenshot),
//      PAUSE_AT ("20,40": pause the page's JS/wasm at those seconds and log its stack, e.g. to see where it spins),
//      PAUSE_EVAL (an expression evaluated in the paused top frame, logged), WEB_PRELOAD (JS run before the page's
//      scripts, e.g. to wrap a browser API and console.trace its callers), WEB_PATH (the page to open, default /),
//      DUMP_FILES ("/tmp/hash.log ...": files copied out of the game's FS into OUT at the end; a directory's files),
//      WEB_NO_COMPRESS (N90: "1" serves the files as they are, not tools/web.sh's .br/.gz),
//      FRAME_STATS (N90: "1", or "a-b" seconds to sum up: frame times from the canvas's getCurrentTexture calls,
//      one per frame aurora draws; per second and summed up in OUT/frames.txt, the summary in the console too),
//      PROFILE (N90: "a-b": Chromium's CPU profile of those seconds in OUT/profile.cpuprofile, for DevTools, and
//      OUT/profile.txt: the main thread's time in wasm, JS, GC, idle, per frame with FRAME_STATS, and the wasm
//      functions with the most self time).
import { spawn } from 'node:child_process';
import { createServer } from 'node:http';
import { appendFileSync, createReadStream, existsSync, mkdirSync, openSync, statSync, writeFileSync } from 'node:fs';
import { basename, extname, join, resolve } from 'node:path';

const [disc, out, secs = '60', query = ''] = process.argv.slice(2);
if (!disc || !out) {
  console.error('usage: node tools/web_check.mjs DISC OUT [SECS] [QUERY]');
  process.exit(2);
}
mkdirSync(out, { recursive: true });
const root = resolve(new URL('..', import.meta.url).pathname, process.env.WEB_ROOT ?? 'build/web');
const webPort = +(process.env.WEB_PORT ?? 8787), cdpPort = +(process.env.CDP_PORT ?? 9339);
const extra = new Map((process.env.WEB_FILES ?? '').split(' ').filter(Boolean).map(f => [basename(f), resolve(f)]));
const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.png': 'image/png' };
const sleep = ms => new Promise(r => setTimeout(r, ms));
const t0 = Date.now();
// console.txt is written as lines come, so a run that's killed still leaves it
writeFileSync(join(out, 'console.txt'), '');
const log = { push: line => appendFileSync(join(out, 'console.txt'), line + '\n') };
const stamp = () => `${((Date.now() - t0) / 1000).toFixed(1)}s`;

const served = [];
const server = createServer((req, res) => {
  const path = decodeURIComponent(new URL(req.url, 'http://x').pathname);
  const file = path.startsWith('/test/') ? extra.get(path.slice(6)) : join(root, path === '/' ? 'index.html' : path);
  if (!file || !file.startsWith(path.startsWith('/test/') ? '/' : root) || !existsSync(file) || !statSync(file).isFile()) {
    res.writeHead(404).end();
    return;
  }
  // N90: a precompressed copy (tools/web.sh's .br/.gz) if the browser takes it, as a server with precompressed
  // files does; WEB_NO_COMPRESS=1 serves the files as they are.
  const accept = String(req.headers['accept-encoding'] ?? '');
  const encoding = process.env.WEB_NO_COMPRESS ? null : [['br', '.br'], ['gzip', '.gz']].find(([e, ext]) =>
    new RegExp(`\\b${e}\\b`).test(accept) && existsSync(file + ext));
  const headers = { 'Content-Type': types[extname(file)] ?? 'application/octet-stream', 'Cache-Control': 'no-store' };
  if (encoding) {
    headers['Content-Encoding'] = encoding[0];
    served.push(`${path} ${encoding[0]} ${statSync(file + encoding[1]).size}`);
  }
  res.writeHead(200, headers);
  createReadStream(encoding ? file + encoding[1] : file).pipe(res);
}).listen(webPort, '127.0.0.1');

// The machine's GPU through Vulkan, in a headless (ozone) window. Chromium's default headless WebGPU is SwiftShader,
// whose canvas loses the device on its second texture ("Could not find SharedImageBackingFactory", Chromium 154).
const GPU_FLAGS = '--use-angle=vulkan --enable-features=Vulkan --use-vulkan=native --ozone-platform=headless';
const chrome = spawn(process.env.CHROME ?? 'chromium', ['--headless=new', '--mute-audio', `--remote-debugging-port=${cdpPort}`,
  `--user-data-dir=${resolve(out, 'profile')}`, '--enable-unsafe-webgpu', '--ignore-gpu-blocklist', '--window-size=1280,960',
  '--enable-logging=stderr', ...(process.env.CHROME_FLAGS ?? GPU_FLAGS).split(' ').filter(Boolean), 'about:blank'],
  { stdio: ['ignore', 'ignore', openSync(join(out, 'chrome.log'), 'w')] });
const finish = code => {
  chrome.kill('SIGKILL');
  server.close();
  process.exit(code);
};
for (const s of ['SIGINT', 'SIGTERM']) process.on(s, () => finish(1));

let targets;
for (let i = 0; i < 100 && !targets; i++) {
  await sleep(200);
  targets = await fetch(`http://127.0.0.1:${cdpPort}/json`).then(r => r.json()).catch(() => null);
}
const ws = new WebSocket(targets.find(t => t.type === 'page').webSocketDebuggerUrl);
await new Promise(r => (ws.onopen = r));
let id = 0;
const waiting = new Map();
let stopped = false;
ws.onmessage = ({ data }) => {
  const m = JSON.parse(data);
  if (m.id && waiting.has(m.id)) waiting.get(m.id)(m), waiting.delete(m.id);
  let line;
  if (m.method === 'Runtime.consoleAPICalled') {
    line = `${m.params.type}: ${m.params.args.map(a => a.value ?? a.description).join(' ')}`;
    if (m.params.type === 'trace') line += '\n' + m.params.stackTrace.callFrames.map(f => `    ${f.functionName}`).join('\n');
  }
  if (m.method === 'Runtime.exceptionThrown') line = `exception: ${m.params.exceptionDetails.exception?.description ?? m.params.exceptionDetails.text}`;
  if (m.method === 'Log.entryAdded') line = `log ${m.params.entry.level}: ${m.params.entry.text}`;
  if (m.method === 'Debugger.paused') {
    line = `paused:\n${m.params.callFrames.map(f => `    ${f.functionName || '?'} ${f.url.split('/').pop()}:${f.location.lineNumber}:${f.location.columnNumber}`).join('\n')}`;
    const resume = () => ws.send(JSON.stringify({ id: 999999, method: 'Debugger.resume' }));
    if (process.env.PAUSE_EVAL) {
      send('Debugger.evaluateOnCallFrame', { callFrameId: m.params.callFrames[0].callFrameId, expression: process.env.PAUSE_EVAL,
        returnByValue: true }).then(r => { log.push(`[${stamp()}] eval: ${JSON.stringify(r.result?.result?.value ?? r.result)}`); resume(); });
    } else {
      resume();
    }
  }
  if (line) {
    log.push(`[${stamp()}] ${line}`);
    if (process.env.STOP_ON && line.includes(process.env.STOP_ON)) stopped = true;
  }
};
const send = (method, params = {}) => new Promise(r => { waiting.set(++id, r); ws.send(JSON.stringify({ id, method, params })); });
await send('Runtime.enable');
await send('Log.enable');
await send('Page.enable');
const pauses = (process.env.PAUSE_AT ?? '').split(',').filter(Boolean).map(Number);
if (pauses.length) await send('Debugger.enable');
if (process.env.WEB_PRELOAD) await send('Page.addScriptToEvaluateOnNewDocument', { source: process.env.WEB_PRELOAD });
const frameStats = process.env.FRAME_STATS;
if (frameStats) {
  await send('Page.addScriptToEvaluateOnNewDocument', { source: `(() => {
    const times = window.__pbFrameTimes = [];
    const get = GPUCanvasContext.prototype.getCurrentTexture;
    GPUCanvasContext.prototype.getCurrentTexture = function () { times.push(performance.now()); return get.call(this); };
  })()` });
}
await send('Page.navigate', { url: `http://127.0.0.1:${webPort}${process.env.WEB_PATH ?? '/'}?${query}` });
await sleep(1500);
if (disc !== '-') {
  const { root: doc } = (await send('DOM.getDocument')).result;
  const { nodeId } = (await send('DOM.querySelector', { nodeId: doc.nodeId, selector: '#pick' })).result;
  await send('DOM.setFileInputFiles', { nodeId, files: [resolve(disc)] });
  log.push(`[${stamp()}] web_check: picked ${disc}`);
}

// Frame times (ms between getCurrentTexture calls), taken from the page every second.
const frameLines = [], frameAll = [];
let frameLast = null;
const pct = (sorted, p) => sorted.length ? sorted[Math.min(sorted.length - 1, Math.floor(p / 100 * sorted.length))] : 0;
const frameSummary = (deltas, label) => {
  const sorted = [...deltas].sort((a, b) => a - b);
  const sum = deltas.reduce((a, b) => a + b, 0);
  return `${label}: ${deltas.length} frames, ${(deltas.length ? 1000 * deltas.length / sum : 0).toFixed(1)} fps, ` +
    `mean ${(deltas.length ? sum / deltas.length : 0).toFixed(2)} ms, p50 ${pct(sorted, 50).toFixed(2)}, ` +
    `p95 ${pct(sorted, 95).toFixed(2)}, p99 ${pct(sorted, 99).toFixed(2)}, max ${(sorted.at(-1) ?? 0).toFixed(2)}`;
};
const takeFrames = async s => {
  const r = await send('Runtime.evaluate', { expression: 'window.__pbFrameTimes ? window.__pbFrameTimes.splice(0) : []', returnByValue: true });
  const deltas = [];
  for (const t of r.result?.result?.value ?? []) {
    if (frameLast !== null) deltas.push(t - frameLast);
    frameLast = t;
  }
  frameAll.push(...deltas.map(d => [s, d]));
  frameLines.push(frameSummary(deltas, `${s}s`));
};

// The CPU profile of PROFILE's seconds: the main thread's time by where it was (wasm code, JS, the browser's own
// work, idle), per frame aurora drew, and the wasm functions with the most self time.
const [profileFrom, profileTo] = (process.env.PROFILE ?? '').split('-').map(Number);
let profileFrames = 0;
const profileSummary = profile => {
  const self = new Map(profile.nodes.map(n => [n.id, 0]));
  const deltas = profile.timeDeltas;
  for (let i = 0; i < profile.samples.length; i++) self.set(profile.samples[i], self.get(profile.samples[i]) + (deltas[i + 1] ?? 0) / 1000);
  const where = { wasm: 0, js: 0, gc: 0, idle: 0, other: 0 };
  const functions = new Map();
  for (const n of profile.nodes) {
    const ms = self.get(n.id), f = n.callFrame;
    const kind = f.url.endsWith('.wasm') ? 'wasm' : f.url ? 'js' : f.functionName === '(garbage collector)' ? 'gc'
      : f.functionName === '(idle)' ? 'idle' : 'other';
    where[kind] += ms;
    if (kind === 'wasm') functions.set(f.functionName, (functions.get(f.functionName) ?? 0) + ms);
  }
  const per = ms => profileFrames ? ` (${(ms / profileFrames).toFixed(2)} ms/frame)` : '';
  const top = [...functions].sort((a, b) => b[1] - a[1]).slice(0, 25);
  return [`profile ${profileFrom}-${profileTo}s, ${profileFrames} frames: ` +
    Object.entries(where).map(([k, ms]) => `${k} ${ms.toFixed(0)} ms${per(ms)}`).join(', '),
    ...top.map(([name, ms]) => `  ${ms.toFixed(1).padStart(8)} ms  ${name}`)].join('\n');
};

const keys = (process.env.WEB_KEYS ?? '').split(',').filter(Boolean).map(k => k.split(':')).map(([s, key]) => [+s, key]);
const every = +(process.env.SHOT_EVERY ?? 10);
const shot = async name => {
  const r = await send('Page.captureScreenshot', { format: 'png' });
  if (r.result?.data) writeFileSync(join(out, `${name}.png`), Buffer.from(r.result.data, 'base64'));
};
for (let s = 1; s <= +secs && !stopped; s++) {
  await sleep(1000);
  if (pauses.includes(s)) ws.send(JSON.stringify({ id: 999998, method: 'Debugger.pause' }));
  for (const [at, key] of keys) {
    if (at !== s) continue;
    for (const type of ['keyDown', 'keyUp']) {
      await send('Input.dispatchKeyEvent', { type, key, code: key.length === 1 ? `Key${key.toUpperCase()}` : key,
        windowsVirtualKeyCode: { Enter: 13, ArrowDown: 40, ArrowUp: 38, ArrowLeft: 37, ArrowRight: 39, Escape: 27, F1: 112, Tab: 9 }[key] ?? key.toUpperCase().charCodeAt(0) });
      await sleep(80);
    }
    log.push(`[${stamp()}] web_check: key ${key}`);
  }
  if (frameStats) await takeFrames(s);
  if (s === profileFrom) {
    await send('Profiler.enable');
    await send('Profiler.setSamplingInterval', { interval: 200 });
    await send('Profiler.start');
  }
  if (s > profileFrom && s <= profileTo) profileFrames += frameAll.filter(([at]) => at === s).length;
  if (s === profileTo) {
    const { profile } = (await send('Profiler.stop')).result;
    writeFileSync(join(out, 'profile.cpuprofile'), JSON.stringify(profile));
    const summary = profileSummary(profile);
    writeFileSync(join(out, 'profile.txt'), summary + '\n');
    log.push(`[${stamp()}] web_check: ${summary.split('\n')[0]}`);
  }
  if (s % every === 0) await shot(`shot-${String(s).padStart(3, '0')}`);
}
if (frameStats) {
  const [from, to] = /^\d+-\d+$/.test(frameStats) ? frameStats.split('-').map(Number) : [0, Infinity];
  const summary = frameSummary(frameAll.filter(([s]) => s > from && s <= to).map(([, d]) => d),
    `frames ${from}-${Number.isFinite(to) ? to : 'end'}s`);
  writeFileSync(join(out, 'frames.txt'), frameLines.join('\n') + '\n' + summary + '\n');
  log.push(`[${stamp()}] web_check: ${summary}`);
}
await shot('shot-last');
// a directory: the files in it (N90)
const dumps = [];
for (const path of (process.env.DUMP_FILES ?? '').split(' ').filter(Boolean)) {
  const r = await send('Runtime.evaluate', { expression: `(() => { try { const FS = pbModule.FS, p = ${JSON.stringify(path)}; return FS.isDir(FS.stat(p).mode) ? FS.readdir(p).filter(n => n !== '.' && n !== '..').map(n => p + '/' + n) : null; } catch (e) { return null; } })()`, returnByValue: true });
  dumps.push(...(r.result?.result?.value ?? [path]));
}
for (const path of dumps) {
  // in 256 KB pieces: one CDP message per piece
  const size = (await send('Runtime.evaluate', { expression: `(() => { try { return pbModule.FS.stat(${JSON.stringify(path)}).size; } catch (e) { return -1; } })()`, returnByValue: true })).result?.result?.value ?? -1;
  log.push(`[${stamp()}] web_check: ${path} ${size < 0 ? 'not found' : `${size} bytes -> ${basename(path)}`}`);
  if (size < 0) continue;
  const parts = [];
  for (let at = 0; at < size; at += 262144) {
    const r = await send('Runtime.evaluate', { expression: `(() => { const b = pbModule.FS.readFile(${JSON.stringify(path)}).subarray(${at}, ${at + 262144}); let s = ''; for (let i = 0; i < b.length; i += 8192) s += String.fromCharCode(...b.subarray(i, i + 8192)); return btoa(s); })()`, returnByValue: true });
    parts.push(Buffer.from(r.result?.result?.value ?? '', 'base64'));
  }
  writeFileSync(join(out, basename(path)), Buffer.concat(parts));
}
if (served.length) log.push(`[${stamp()}] web_check: served compressed: ${served.join(', ')}`);
const errors = await send('Runtime.evaluate', { expression: "document.getElementById('error').textContent", returnByValue: true });
log.push(`[${stamp()}] web_check: page errors: ${JSON.stringify(errors.result?.result?.value ?? '')}`);
finish(0);
