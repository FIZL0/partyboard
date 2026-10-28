// N92: two browser peers in one headless Chromium (tools/web.sh's build/web): tab A hosts a lobby, tab B joins it
// over WebRTC (net/src/rtc.rs), the codes go both ways through this script as a player would copy them, and the
// match runs in lockstep. As tools/web_check.mjs (server, GPU flags, console capture), with two tabs on two origins
// (127.0.0.1 and localhost: separate OPFS and IndexedDB, so separate discs in storage and user dirs).
//
// The lobby is driven through the game's own C ABI (exported by cmake/web.cmake: pbnet_lobby_host / _join /
// _answer / _ready / _start / _json), which is what the Online menu calls; the match then starts as from the menu
// (netplay_lobby_poll). Each tab gets a fake standard gamepad (navigator.getGamepads) that taps A once the match
// runs, so both human seats play their turns, and one START 300 frames after a minigame's instructions start (the
// console's "resync point ... minigame", N52's instDll point), another at 700 frames if they still show (START in
// instDll's intro, or a second one in its wipe, goes back to the menu: runs 5-8 of the N92 journal).
//
// Usage: node tools/web_pair.mjs DISC OUT [SECS]
//   SECS: how long the match runs after the start (default 120). OUT gets a.txt / b.txt (each tab's console),
//   shots a-*.png / b-*.png, lobby-*.json, and pair.txt (the run's log: codes, frames, waits, verdict).
// Env: WEB_PORT (8788: not web_check.mjs's, which other slots may be running), CHROME, CHROME_FLAGS, SHOT_EVERY (30), PAIR_CPUS ("2,3": seats the host gives
//      the computer, default both free seats), PAIR_MASH (0: no fake gamepad presses), PAIR_ARGS (the game's command
//      line in both tabs, e.g. '--hash-log /tmp/h.log'), DUMP_FILES ("/tmp/h.log": copied out of each tab's FS into
//      OUT as a-h.log / b-h.log at the end), PAIR_KEEP_PROFILE (keep OUT/profile, removed otherwise), PAIR_SEED (the match seed, e.g. 0x18E2A0AF4FE4E301; default from the clock).
// Exit status: 0 if both peers started the match, advanced past frame 600 and saw no desync; else 1.
import { spawn } from 'node:child_process';
import { createServer } from 'node:http';
import { appendFileSync, createReadStream, existsSync, mkdirSync, openSync, readFileSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { extname, join, resolve } from 'node:path';

const [disc, out, secs = '120'] = process.argv.slice(2);
if (!disc || !out) {
  console.error('usage: node tools/web_pair.mjs DISC OUT [SECS]');
  process.exit(2);
}
mkdirSync(out, { recursive: true });
const root = resolve(new URL('..', import.meta.url).pathname, 'build/web');
const webPort = +(process.env.WEB_PORT ?? 8788);
const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.png': 'image/png' };
const sleep = ms => new Promise(r => setTimeout(r, ms));
const t0 = Date.now();
const stamp = () => `${((Date.now() - t0) / 1000).toFixed(1)}s`;
for (const f of ['pair.txt', 'a.txt', 'b.txt']) writeFileSync(join(out, f), '');
const say = line => {
  appendFileSync(join(out, 'pair.txt'), `[${stamp()}] ${line}\n`);
  console.log(`[${stamp()}] ${line}`);
};

const server = createServer((req, res) => {
  const path = decodeURIComponent(new URL(req.url, 'http://x').pathname);
  const file = join(root, path === '/' ? 'index.html' : path);
  if (!file.startsWith(root) || !existsSync(file) || !statSync(file).isFile()) {
    res.writeHead(404).end();
    return;
  }
  res.writeHead(200, { 'Content-Type': types[extname(file)] ?? 'application/octet-stream', 'Cache-Control': 'no-store' });
  createReadStream(file).pipe(res);
}).listen(webPort, '127.0.0.1');
// a port in use (another run) must stop this one, not let it talk to someone else's page
server.on('error', e => { console.error(`web_pair: ${e.message}`); process.exit(1); });

const GPU_FLAGS = '--use-angle=vulkan --enable-features=Vulkan --use-vulkan=native --ozone-platform=headless';
// The DevTools port is the one this Chromium picks (written to its profile's DevToolsActivePort): a fixed port that
// a leftover Chromium still holds would hand this run that browser and its old tabs (N92 runs 8 and 9).
rmSync(resolve(out, 'profile', 'DevToolsActivePort'), { force: true });
const chrome = spawn(process.env.CHROME ?? 'chromium', ['--headless=new', '--mute-audio', '--remote-debugging-port=0',
  `--user-data-dir=${resolve(out, 'profile')}`, '--enable-unsafe-webgpu', '--ignore-gpu-blocklist', '--window-size=1280,960',
  // both tabs stay "visible": no timer throttling for the one that isn't in front
  '--disable-background-timer-throttling', '--disable-renderer-backgrounding', '--disable-backgrounding-occluded-windows',
  '--enable-logging=stderr', ...(process.env.CHROME_FLAGS ?? GPU_FLAGS).split(' ').filter(Boolean), 'about:blank'],
  { stdio: ['ignore', 'ignore', openSync(join(out, 'chrome.log'), 'w')], detached: true });
let verdict = 1;
const finish = async () => {
  // the whole process group: renderers and the GPU process too
  try { process.kill(-chrome.pid, 'SIGKILL'); } catch (e) { chrome.kill('SIGKILL'); }
  server.close();
  // the profile holds both origins' copies of the disc (OPFS): 1.2 GB
  if (!process.env.PAIR_KEEP_PROFILE) {
    await sleep(500);
    rmSync(resolve(out, 'profile'), { recursive: true, force: true });
  }
  process.exit(verdict);
};
for (const s of ['SIGINT', 'SIGTERM']) process.on(s, finish);
process.on('uncaughtException', e => { say(`error: ${e.stack ?? e}`); finish(); });

let targets, cdpPort;
for (let i = 0; i < 100 && !targets; i++) {
  await sleep(200);
  try {
    cdpPort = +readFileSync(resolve(out, 'profile', 'DevToolsActivePort'), 'utf8').split('\n')[0];
  } catch (e) {
    continue;
  }
  targets = await fetch(`http://127.0.0.1:${cdpPort}/json`).then(r => r.json()).catch(() => null);
}
const first = targets.find(t => t.type === 'page');
const second = await fetch(`http://127.0.0.1:${cdpPort}/json/new?about:blank`, { method: 'PUT' }).then(r => r.json());

// A fake standard gamepad: SDL's Emscripten joystick reads navigator.getGamepads(); window.__pbMash taps A.
const PAD = `(() => {
  const pad = { id: 'pbtest pad (STANDARD GAMEPAD)', index: 0, connected: true, mapping: 'standard', timestamp: 1,
    axes: [0, 0, 0, 0], buttons: Array.from({ length: 17 }, () => ({ pressed: false, touched: false, value: 0 })) };
  navigator.getGamepads = () => [pad, null, null, null];
  window.__pbConnect = () => { const e = new Event('gamepadconnected'); e.gamepad = pad; dispatchEvent(e); };
  // A taps, and single START taps at the times in __pbStarts (the minigame instructions, instDll, leave on START only;
  // a second START reaches the system pause, whose quit goes back to the menu). A rests from 1 s before each START to
  // 6 s after it.
  setInterval(() => {
    const now = Date.now(), starts = window.__pbStarts ?? [];
    const start = starts.some(t => now >= t && now < t + 100);
    const rest = starts.some(t => now >= t - 1000 && now < t + 6000);
    const down = !!window.__pbMash && now % 500 < 80;
    for (const [b, on] of [[0, down && !rest], [9, start]]) {
      if (pad.buttons[b].pressed !== on) {
        pad.buttons[b].pressed = on; pad.buttons[b].value = on ? 1 : 0; pad.timestamp++;
      }
    }
  }, 8);
})();`;

async function tab(name, target, origin) {
  const ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise(r => (ws.onopen = r));
  let id = 0;
  const waiting = new Map();
  const lines = [];
  // the last minigame point's frame (-1: none pending) and how many STARTs it got
  const mg = { at: -1, stage: 0 };
  ws.onmessage = ({ data }) => {
    const m = JSON.parse(data);
    if (m.id && waiting.has(m.id)) waiting.get(m.id)(m), waiting.delete(m.id);
    let line;
    if (m.method === 'Runtime.consoleAPICalled') line = `${m.params.type}: ${m.params.args.map(a => a.value ?? a.description).join(' ')}`;
    if (m.method === 'Runtime.exceptionThrown') line = `exception: ${m.params.exceptionDetails.exception?.description ?? m.params.exceptionDetails.text}`;
    if (m.method === 'Log.entryAdded') line = `log ${m.params.entry.level}: ${m.params.entry.text}`;
    if (line) {
      const js = expression => ws.send(JSON.stringify({ id: 999990, method: 'Runtime.evaluate', params: { expression } }));
      // the minigame's instructions: START once they show (the main loop times it in game frames)
      const point = line.match(/netplay: resync point at frame (\d+): minigame/);
      if (point) mg.at = +point[1], mg.stage = 0;
      // the minigame started: no more START
      if (line.includes('Start New OVL') && !line.includes('Start New OVL 3 ')) mg.at = -1, js('window.__pbStarts = []');
      lines.push(line);
      appendFileSync(join(out, `${name}.txt`), `[${stamp()}] ${line}\n`);
    }
  };
  // a page that crashed or hangs answers nothing: give up on a call after 20 s
  const send = (method, params = {}) => Promise.race([
    new Promise(r => { waiting.set(++id, r); ws.send(JSON.stringify({ id, method, params })); }),
    sleep(20000).then(() => { throw new Error(`${name}: ${method} got no answer`); })]);
  const evaluate = async expression => {
    const r = await send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
    if (r.result?.exceptionDetails) throw new Error(`${name}: ${expression}: ${r.result.exceptionDetails.exception?.description}`);
    return r.result?.result?.value;
  };
  await send('Runtime.enable');
  await send('Log.enable');
  await send('Page.enable');
  await send('Page.addScriptToEvaluateOnNewDocument', { source: PAD });
  const args = process.env.PAIR_ARGS ? `?args=${encodeURIComponent(process.env.PAIR_ARGS)}` : '';
  await send('Page.navigate', { url: `${origin}/${args}` });
  await sleep(1500);
  const { root: doc } = (await send('DOM.getDocument')).result;
  const { nodeId } = (await send('DOM.querySelector', { nodeId: doc.nodeId, selector: '#pick' })).result;
  await send('DOM.setFileInputFiles', { nodeId, files: [resolve(disc)] });
  say(`${name}: ${origin} picked ${disc}`);
  // the C ABI with strings: pbnet functions copy what they keep
  await evaluate(`window.__pb = {
    str(s) { return pbModule.stringToNewUTF8(s); },
    call(fn, ...strs) { const p = strs.map(s => typeof s === 'string' ? this.str(s) : s); const r = pbModule[fn](...p);
      p.forEach((x, i) => typeof strs[i] === 'string' && pbModule._free(x)); return r; },
    json() { const n = pbModule._pbnet_lobby_json(0, 0); const b = pbModule._malloc(n + 1); pbModule._pbnet_lobby_json(b, n + 1);
      const s = pbModule.UTF8ToString(b); pbModule._free(b); return JSON.parse(s); },
    why(fn, ...args) { const b = pbModule._malloc(256); const r = this.call(fn, ...args, b, 256); const w = pbModule.UTF8ToString(b);
      pbModule._free(b); return r ? '' : w || 'refused'; },
  }; true`).catch(() => {});
  const shot = async label => {
    const r = await send('Page.captureScreenshot', { format: 'png' });
    if (r.result?.data) writeFileSync(join(out, `${name}-${label}.png`), Buffer.from(r.result.data, 'base64'));
  };
  const dump = async path => {
    const size = await evaluate(`(() => { try { return pbModule.FS.stat(${JSON.stringify(path)}).size; } catch (e) { return -1; } })()`);
    if (size < 0) return say(`${name}: ${path} not found`);
    const parts = [];
    for (let at = 0; at < size; at += 262144) {
      const b64 = await evaluate(`(() => { const b = pbModule.FS.readFile(${JSON.stringify(path)}).subarray(${at}, ${at + 262144}); let s = ''; for (let i = 0; i < b.length; i += 8192) s += String.fromCharCode(...b.subarray(i, i + 8192)); return btoa(s); })()`);
      parts.push(Buffer.from(b64 ?? '', 'base64'));
    }
    writeFileSync(join(out, `${name}-${path.split('/').pop()}`), Buffer.concat(parts));
    say(`${name}: ${path} ${size} bytes`);
  };
  return { name, send, evaluate, lines, shot, dump, mg };
}

const until = async (what, secs, f) => {
  for (const end = Date.now() + secs * 1000; Date.now() < end; await sleep(250)) {
    const v = await f().catch(() => undefined);
    if (v) return v;
  }
  throw new Error(`timed out: ${what}`);
};

try {
  const a = await tab('a', first, `http://127.0.0.1:${webPort}`);
  const b = await tab('b', second, `http://localhost:${webPort}`);
  for (const t of [a, b]) {
    await until(`${t.name} boots`, 120, () => t.evaluate('typeof pbModule !== "undefined" && !!pbModule._pbnet_lobby_json'));
    // __pb was defined before the module existed: define it again now
    await t.evaluate(`typeof __pb !== 'undefined'`).then(ok => ok || say(`${t.name}: no __pb helper`));
  }
  // the title screen first (bootDll done), as a player opening the Online menu would be
  await sleep(25000);
  for (const t of [a, b]) {
    await t.evaluate('__pbConnect(), true');
    await t.shot('title');
  }
  const setup = name => JSON.stringify({ build: 'web-pair', disc: 'GMPE01-01', relay: 'off', short: true,
    players: [{ name, key: `key-${name}` }] });
  if (!await a.evaluate(`__pb.call('_pbnet_lobby_host', ${JSON.stringify(setup('Host'))})`)) throw new Error('a: host refused');
  const code = await until('the host\'s code', 30, async () => (await a.evaluate('__pb.json()')).short);
  say(`a: hosting, code ${code.length} chars: ${code}`);
  if (!await b.evaluate(`__pb.call('_pbnet_lobby_join', ${JSON.stringify(setup('Guest'))}, ${JSON.stringify(code)})`)) throw new Error('b: join refused');
  const answer = await until('the guest\'s answer', 30, async () => (await b.evaluate('__pb.json()')).answer);
  say(`b: answer ${answer.length} chars: ${answer}`);
  const why = await a.evaluate(`__pb.why('_pbnet_lobby_answer', ${JSON.stringify(answer)})`);
  if (why) throw new Error(`a: answer refused: ${why}`);
  say('a: answer taken');
  await until('the guest is seated', 40, async () => (await b.evaluate('__pb.json()')).room?.seats?.filter(Boolean).length === 2);
  say('b: seated');
  const fresh = await until('the host\'s next code', 15, async () => { const c = (await a.evaluate('__pb.json()')).short; return c && c !== code && c; });
  say(`a: the next guest's code is a fresh offer (${fresh.slice(0, 16)}...)`);
  for (const s of (process.env.PAIR_CPUS ?? '2,3').split(',').filter(Boolean)) await a.evaluate(`pbModule._pbnet_lobby_cpu(${+s}, 1)`);
  await sleep(500);
  if (!await b.evaluate('pbModule._pbnet_lobby_ready(1)')) throw new Error('b: ready refused');
  const started = await until('the start', 20, async () => {
    const w = await a.evaluate(`__pb.why('_pbnet_lobby_start', ${BigInt(process.env.PAIR_SEED ?? 0)}n)`);
    return w === '' ? 'ok' : (say(`a: start: ${w}`), undefined);
  });
  say(`a: started (${started})`);
  for (const t of [a, b]) writeFileSync(join(out, `lobby-${t.name}.json`), JSON.stringify(await t.evaluate('__pb.json()'), null, 2));
  if (process.env.PAIR_MASH !== '0') for (const t of [a, b]) await t.evaluate('window.__pbMash = true');
  const every = +(process.env.SHOT_EVERY ?? 30);
  let frames = [0, 0];
  for (let s = 1; s <= +secs; s++) {
    await sleep(1000);
    frames = [await a.evaluate('pbModule._pbnet_frame()'), await b.evaluate('pbModule._pbnet_frame()')];
    if (s % 10 === 0) {
      const wait = [await a.evaluate('pbModule._pbnet_waiting_on()'), await b.evaluate('pbModule._pbnet_waiting_on()')];
      say(`lockstep frames a=${frames[0]} b=${frames[1]} (waiting on a=${wait[0]} b=${wait[1]})`);
    }
    if (s % every === 0) for (const t of [a, b]) await t.shot(`match-${String(s).padStart(3, '0')}`);
    // START 300 frames into instDll (its intro is over: an earlier START, or a second one in its wipe, sent the
    // party back to the menu), again 700 frames in if it still shows; whatever the frame rate
    for (const [i, t] of [a, b].entries()) {
      if (t.mg.at < 0 || frames[i] < t.mg.at) continue;
      if ((t.mg.stage === 0 && frames[i] >= t.mg.at + 300) || (t.mg.stage === 1 && frames[i] >= t.mg.at + 700)) {
        t.mg.stage++;
        await t.evaluate('window.__pbStarts = [Date.now() + 1000], true');
        await t.shot(`mg-${String(s).padStart(3, '0')}`);
      }
    }
  }
  for (const t of [a, b]) await t.shot('last');
  for (const path of (process.env.DUMP_FILES ?? '').split(' ').filter(Boolean)) for (const t of [a, b]) await t.dump(path).catch(e => say(String(e)));
  // pbnet's desync input logs (the current directory: the harness opens no Online menu, which sets the user dir)
  for (const t of [a, b]) {
    const logs = await t.evaluate(`pbModule.FS.readdir('/').filter(n => n.startsWith('desync-'))`).catch(() => []);
    for (const l of logs ?? []) await t.dump(`/${l}`).catch(e => say(String(e)));
  }
  const desyncs = [a, b].map(t => t.lines.filter(l => /desync|out of sync|mismatch/i.test(l)).length);
  const startedLines = [a, b].map(t => t.lines.some(l => l.includes('netplay: lobby started')));
  say(`started: a=${startedLines[0]} b=${startedLines[1]}; desync lines: a=${desyncs[0]} b=${desyncs[1]}; frames a=${frames[0]} b=${frames[1]}`);
  verdict = startedLines.every(Boolean) && frames.every(f => f > 600) && desyncs.every(n => n === 0) ? 0 : 1;
  say(verdict === 0 ? 'PASS' : 'FAIL');
} catch (e) {
  say(`error: ${e.stack ?? e}`);
}
finish();
