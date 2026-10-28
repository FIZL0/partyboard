// N96 (a copy of tools/web_pair.mjs): with PAIR_DESKTOP=<dir>, one browser tab (b) joins a desktop game's lobby
// hosted for browsers; the desktop (harness/n96_harness.inc) and this script swap code, answer and start through
// files in <dir>.
//
// N92: two browser peers in one headless Chromium (tools/web.sh's build/web): tab A hosts a lobby, tab B joins it
// over WebRTC (net/src/rtc.rs), the codes go both ways through this script as a player would copy them, and the
// match runs in lockstep. As tools/web_check.mjs (server, GPU flags, console capture), with two tabs on two origins
// (127.0.0.1 and localhost: separate OPFS and IndexedDB, so separate discs in storage and user dirs).
//
// The lobby is driven through the game's own C ABI (exported by cmake/web.cmake: pbnet_lobby_host / _join /
// _answer / _ready / _start / _json), which is what the Online menu calls; the match then starts as from the menu
// (netplay_lobby_poll). Each tab gets a fake standard gamepad (navigator.getGamepads) that taps A once the match
// runs, so both human seats play their turns, and one START 300 frames after a minigame's instructions start (the
// console's "resync point ... minigame", N52's instDll point), another at 700 frames if they still show.
// N97: the START that sent N92's matches to the menu from instDll was not instDll's: the title's BootExec (bootDll)
// ran on under the match (N99), and its "press START" called modeseldll. A run whose match ends at the menu without
// the party's results now fails, and the summary says how far the match got (turn, minigames started).
//
// N98: up to four tabs (PAIR_TABS; the host relays guest to guest), the lobby through the Online menu instead
// (PAIR_MENU=1: real key presses over CDP, the codes through the page's clipboard bridge: Ctrl+V -> pb.js ->
// pb_web_paste, copies -> navigator.clipboard; the browser's clipboard permission granted over CDP), and a guest's
// mid-match drop and redial (PAIR_DROP).
//
// Usage: node tools/web_pair.mjs DISC OUT [SECS]
//   SECS: how long the match runs after the start (default 120). OUT gets a.txt / b.txt (each tab's console),
//   shots a-*.png / b-*.png, lobby-*.json, and pair.txt (the run's log: codes, frames, waits, verdict).
// Env: WEB_ROOT (the build to serve, default build/web), WEB_PORT (8788: not web_check.mjs's, which other slots may be running), CHROME, CHROME_FLAGS, SHOT_EVERY (30), PAIR_CPUS ("2,3": seats the host gives
//      the computer, default both free seats), PAIR_MASH (0: no fake gamepad presses), PAIR_ARGS (the game's command
//      line in both tabs, e.g. '--hash-log /tmp/h.log'), DUMP_FILES ("/tmp/h.log": copied out of each tab's FS into
//      OUT as a-h.log / b-h.log at the end), PAIR_KEEP_PROFILE (keep OUT/profile, removed otherwise), PAIR_SEED (the match seed, e.g. 0x18E2A0AF4FE4E301; default from the clock).
//      N98: PAIR_TABS (2: tabs a, b, c, d: 127.0.0.1 and localhost on WEB_PORT, then on WEB_PORT+1), PAIR_MENU=1 (the
//      lobby through the menu: host F1 > Online > Host Game, copies its invite link; guest b presses Ctrl+V, Join >
//      Paste Code > Join Game; guest c opens the link (on its own origin) and picks its disc there, as a friend's first
//      visit; d as b; each guest Copy Answer, the host Ctrl+V and Paste Answer; Ready and Start Match in the menu;
//      relay setting the profile's default, n0 = Google STUN), PAIR_DROP=S (S seconds into the match guest b's
//      peer connections close; its hold screen's Copy Answer, the host's Ctrl+V and Paste Answer on its waiting screen
//      bring it back: needs PAIR_MENU), PAIR_SEED ignored with PAIR_MENU (the menu starts with seed 0: the clock).
// Exit status: 0 if every peer started the match, advanced past frame 600, saw no desync and did not leave the match
// for the menu (N97; and with PAIR_DROP, the guest came back and the match went on 300 frames); else 1.
import { spawn } from 'node:child_process';
import { createServer } from 'node:http';
import { appendFileSync, createReadStream, existsSync, mkdirSync, openSync, readFileSync, renameSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { extname, join, resolve } from 'node:path';

const [disc, out, secs = '120'] = process.argv.slice(2);
if (!disc || !out) {
  console.error('usage: node tools/web_pair.mjs DISC OUT [SECS]');
  process.exit(2);
}
mkdirSync(out, { recursive: true });
const root = resolve(new URL('..', import.meta.url).pathname, process.env.WEB_ROOT ?? 'build/web');
const webPort = +(process.env.WEB_PORT ?? 8788);
const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.png': 'image/png' };
const sleep = ms => new Promise(r => setTimeout(r, ms));
const t0 = Date.now();
const stamp = () => `${((Date.now() - t0) / 1000).toFixed(1)}s`;
const desktop = process.env.PAIR_DESKTOP;
const nTabs = desktop ? 1 : Math.min(Math.max(+(process.env.PAIR_TABS ?? 2), 2), 4);
const names = desktop ? ['b'] : ['a', 'b', 'c', 'd'].slice(0, nTabs);
const menu = process.env.PAIR_MENU === '1';
for (const f of ['pair.txt', ...names.map(n => `${n}.txt`)]) writeFileSync(join(out, f), '');
const say = line => {
  appendFileSync(join(out, 'pair.txt'), `[${stamp()}] ${line}\n`);
  console.log(`[${stamp()}] ${line}`);
};

const serve = (req, res) => {
  const path = decodeURIComponent(new URL(req.url, 'http://x').pathname);
  const file = join(root, path === '/' ? 'index.html' : path);
  if (!file.startsWith(root) || !existsSync(file) || !statSync(file).isFile()) {
    res.writeHead(404).end();
    return;
  }
  res.writeHead(200, { 'Content-Type': types[extname(file)] ?? 'application/octet-stream', 'Cache-Control': 'no-store' });
  createReadStream(file).pipe(res);
};
// N98: tabs c and d on a second port (two more origins)
const servers = [webPort, webPort + 1].slice(0, nTabs > 2 ? 2 : 1).map(port => {
  const server = createServer(serve).listen(port, '127.0.0.1');
  // a port in use (another run) must stop this one, not let it talk to someone else's page
  server.on('error', e => { console.error(`web_pair: ${e.message}`); process.exit(1); });
  return server;
});
const origins = [`http://127.0.0.1:${webPort}`, `http://localhost:${webPort}`, `http://127.0.0.1:${webPort + 1}`, `http://localhost:${webPort + 1}`];

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
  servers.forEach(s => s.close());
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
const pages = [targets.find(t => t.type === 'page')];
while (pages.length < nTabs) pages.push(await fetch(`http://127.0.0.1:${cdpPort}/json/new?about:blank`, { method: 'PUT' }).then(r => r.json()));

// A fake standard gamepad: SDL's Emscripten joystick reads navigator.getGamepads(); window.__pbMash taps A.
const PAD = `(() => {
  const pad = { id: 'pbtest pad (STANDARD GAMEPAD)', index: 0, connected: true, mapping: 'standard', timestamp: 1,
    axes: [0, 0, 0, 0], buttons: Array.from({ length: 17 }, () => ({ pressed: false, touched: false, value: 0 })) };
  navigator.getGamepads = () => [pad, null, null, null];
  window.__pbConnect = () => { const e = new Event('gamepadconnected'); e.gamepad = pad; dispatchEvent(e); };
  // A taps, and single START taps at the times in __pbStarts (the minigame instructions, instDll, leave on START only).
  // A rests from 1 s before each START to 6 s after it.
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
  // N98: the page's peer connections (PAIR_DROP closes them; their descriptions say which candidates were gathered)
  const RTC = window.RTCPeerConnection;
  window.__pbPcs = [];
  if (RTC) window.RTCPeerConnection = function (...args) { const pc = new RTC(...args); window.__pbPcs.push(pc); return pc; };
})();`;

async function tab(name, target, origin, url) {
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
  // N98: the clipboard for the menu's copies and Ctrl+V (as a player who allowed it), and every tab "focused" (the
  // async clipboard API needs a focused document; only one tab of a window would be)
  await send('Browser.grantPermissions', { permissions: ['clipboardReadWrite', 'clipboardSanitizedWrite'], origin });
  await send('Emulation.setFocusEmulationEnabled', { enabled: true });
  const args = process.env.PAIR_ARGS ? `?args=${encodeURIComponent(process.env.PAIR_ARGS)}` : '';
  await send('Page.navigate', { url: url ?? `${origin}/${args}` });
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
  // N98: a player's key press (CDP key events reach SDL as the keyboard's), the menu's focused text, and moving the
  // focus to the control whose text starts with `label` (Down, Up, then sideways, as a player would look for it)
  const KEYS = { F1: 112, Enter: 13, Escape: 27, ArrowDown: 40, ArrowUp: 38, ArrowLeft: 37, ArrowRight: 39, PageDown: 34, PageUp: 33 };
  const key = async (k, ctrl = false) => {
    const vk = KEYS[k] ?? k.toUpperCase().charCodeAt(0);
    for (const type of ['rawKeyDown', 'keyUp']) {
      await send('Input.dispatchKeyEvent', { type, key: k, code: KEYS[k] ? k : `Key${k.toUpperCase()}`, windowsVirtualKeyCode: vk, modifiers: ctrl ? 2 : 0 });
    }
    await sleep(350);
  };
  const focus = async () => (await evaluate(`(() => { const n = pbModule._pb_web_ui_focus(0, 0); const b = pbModule._malloc(n + 1);
    pbModule._pb_web_ui_focus(b, n + 1); const s = pbModule.UTF8ToString(b); pbModule._free(b); return s; })()`) ?? '').replace(/\s+/g, ' ').trim();
  const find = async label => {
    // (focus on a pane itself moves only Up, to the tab, and from there Down into the pane; a modal's buttons are
    // in a row)
    for (const dir of ['ArrowDown', 'ArrowUp', 'ArrowDown', 'ArrowRight', 'ArrowLeft']) {
      let last = null, still = 0;
      for (let i = 0; i < 30; i++) {
        const f = await focus();
        if (f.startsWith(label)) return f;
        // the end of the list: the focus stops moving
        still = f === last ? still + 1 : 0;
        if (still >= 3) break;
        last = f;
        await key(dir);
      }
    }
    throw new Error(`${name}: no "${label}" in the menu (focus: ${await focus()})`);
  };
  const press = async label => {
    const f = await find(label);
    say(`${name}: press [${f}]`);
    await key('Enter');
  };
  const clipboard = () => evaluate('navigator.clipboard.readText()');
  return { name, origin, send, evaluate, lines, shot, dump, mg, key, focus, find, press, clipboard };
}

const until = async (what, secs, f) => {
  for (const end = Date.now() + secs * 1000; Date.now() < end; await sleep(250)) {
    const v = await f().catch(() => undefined);
    if (v) return v;
  }
  throw new Error(`timed out: ${what}`);
};

// The lobby through the exported C ABI (N92): the host's code and each guest's answer copied by this script.
async function lobbyByAbi(host, guests) {
  const setup = name => JSON.stringify({ build: 'web-pair', disc: 'GMPE01-01', relay: 'off', short: true,
    players: [{ name, key: `key-${name}` }] });
  if (!await host.evaluate(`__pb.call('_pbnet_lobby_host', ${JSON.stringify(setup('Host'))})`)) throw new Error('a: host refused');
  let code = await until('the host\'s code', 30, async () => (await host.evaluate('__pb.json()')).short);
  say(`a: hosting, code ${code.length} chars: ${code}`);
  for (const [i, g] of guests.entries()) {
    if (!await g.evaluate(`__pb.call('_pbnet_lobby_join', ${JSON.stringify(setup(`Guest${i + 1}`))}, ${JSON.stringify(code)})`)) throw new Error(`${g.name}: join refused`);
    const answer = await until(`${g.name}'s answer`, 30, async () => (await g.evaluate('__pb.json()')).answer);
    say(`${g.name}: answer ${answer.length} chars: ${answer}`);
    const why = await host.evaluate(`__pb.why('_pbnet_lobby_answer', ${JSON.stringify(answer)})`);
    if (why) throw new Error(`a: answer refused: ${why}`);
    say('a: answer taken');
    await until(`${g.name} is seated`, 40, async () => (await g.evaluate('__pb.json()')).room?.seats?.filter(Boolean).length === i + 2);
    say(`${g.name}: seated`);
    const fresh = await until('the host\'s next code', 15, async () => { const c = (await host.evaluate('__pb.json()')).short; return c && c !== code && c; });
    say(`a: the next guest's code is a fresh offer (${fresh.slice(0, 16)}...)`);
    code = fresh;
  }
  for (const s of (process.env.PAIR_CPUS ?? '2,3').split(',').filter(Boolean).filter(s => +s > guests.length)) {
    await host.evaluate(`pbModule._pbnet_lobby_cpu(${+s}, 1)`);
  }
  await sleep(500);
  for (const g of guests) if (!await g.evaluate('pbModule._pbnet_lobby_ready(1)')) throw new Error(`${g.name}: ready refused`);
  const started = await until('the start', 20, async () => {
    const w = await host.evaluate(`__pb.why('_pbnet_lobby_start', ${BigInt(process.env.PAIR_SEED ?? 0)}n)`);
    return w === '' ? 'ok' : (say(`a: start: ${w}`), undefined);
  });
  say(`a: started (${started})`);
}

// N96: tab b joins the desktop's lobby: its code from code.txt, the answer to answer.txt, start.txt when ready.
async function lobbyWithDesktop(g) {
  const rd = n => { try { return readFileSync(join(desktop, n), 'utf8').trim(); } catch (e) { return ''; } };
  const wr = (n, s) => { writeFileSync(join(desktop, `${n}.tmp`), `${s}\n`); renameSync(join(desktop, `${n}.tmp`), join(desktop, n)); };
  const code = await until('the desktop\'s code', 180, async () => rd('code.txt'));
  say(`desktop: hosting, code ${code.length} chars: ${code}`);
  const setup = JSON.stringify({ build: 'web-pair', disc: 'GMPE01-01', relay: 'off', short: true, players: [{ name: 'Web', key: 'key-web' }] });
  if (!await g.evaluate(`__pb.call('_pbnet_lobby_join', ${JSON.stringify(setup)}, ${JSON.stringify(code)})`)) throw new Error('b: join refused');
  const answer = await until('b\'s answer', 30, async () => (await g.evaluate('__pb.json()')).answer);
  say(`b: answer ${answer.length} chars: ${answer}`);
  wr('answer.txt', answer);
  // the desktop gives seats 2 and 3 to the computer once b is seated: ready for that line-up
  await until('b is seated', 60, async () => (await g.evaluate('__pb.json()')).room?.seats?.filter(Boolean).length >= 4);
  say('b: seated in the desktop\'s room');
  await sleep(500);
  if (!await g.evaluate('pbModule._pbnet_lobby_ready(1)')) throw new Error('b: ready refused');
  wr('start.txt', process.env.PAIR_SEED ?? '0');
  await until('the desktop started', 60, async () => rd('started.txt'));
  say('desktop: started');
}

// N96, PAIR_DESKTOP_GUEST: tab b hosts, the desktop joins with code.txt and sends its answer in answer.txt.
async function lobbyForDesktop(h) {
  const rd = n => { try { return readFileSync(join(desktop, n), 'utf8').trim(); } catch (e) { return ''; } };
  const wr = (n, s) => { writeFileSync(join(desktop, `${n}.tmp`), `${s}\n`); renameSync(join(desktop, `${n}.tmp`), join(desktop, n)); };
  const setup = JSON.stringify({ build: 'web-pair', disc: 'GMPE01-01', relay: 'off', short: true, players: [{ name: 'Web', key: 'key-web' }] });
  if (!await h.evaluate(`__pb.call('_pbnet_lobby_host', ${JSON.stringify(setup)})`)) throw new Error('b: host refused');
  const code = await until('b\'s code', 30, async () => (await h.evaluate('__pb.json()')).short);
  say(`b: hosting, code ${code.length} chars: ${code}`);
  wr('code.txt', code);
  const answer = await until('the desktop\'s answer', 180, async () => rd('answer.txt'));
  say(`desktop: answer ${answer.length} chars: ${answer}`);
  const why = await h.evaluate(`__pb.why('_pbnet_lobby_answer', ${JSON.stringify(answer)})`);
  if (why) throw new Error(`b: answer refused: ${why}`);
  await until('the desktop is seated', 60, async () => (await h.evaluate('__pb.json()')).room?.seats?.filter(Boolean).length >= 2);
  say('desktop: seated in b\'s room');
  await h.evaluate('pbModule._pbnet_lobby_cpu(2, 1)');
  await h.evaluate('pbModule._pbnet_lobby_cpu(3, 1)');
  await until('the start', 60, async () => {
    const w = await h.evaluate(`__pb.why('_pbnet_lobby_start', ${BigInt(process.env.PAIR_SEED ?? 0)}n)`);
    return w === '' ? 'ok' : undefined;
  });
  say('b: started');
}

// N98: the lobby through the Online menu, keys only; the codes go through the clipboard as a player copies them.
// `linkTab` (a guest that opens the invite link) is created here once the link exists.
async function lobbyByMenu(host, guests, linkTab) {
  const json = t => t.evaluate('__pb.json()');
  const openOnline = async t => {
    await t.key('F1');
    await until(`${t.name}: the menu bar`, 10, async () => (await t.focus()) === 'Settings');
    await t.key('ArrowRight');
    await t.key('Enter');
    await until(`${t.name}: the Online window`, 10, async () => (await t.focus()) === 'Host');
  };
  await openOnline(host);
  await host.press('Host Game');
  await until('a: hosting', 30, async () => (await json(host)).short);
  await host.find('Code rtco');
  await host.key('Enter');
  const link = await until('the invite link on the clipboard', 10, async () => { const c = await host.clipboard(); return c.includes('?code=rtco') && c; });
  say(`a: copied the invite link (${link.length} chars): ${link.slice(0, 60)}...`);
  let code = link;
  for (const [i, g] of [...guests, ...(linkTab ? [null] : [])].entries()) {
    let t = g;
    if (!t) {
      // the friend opens the link: on their own origin (their browser's storage), first visit, then picks their disc
      const url = new URL(code);
      const own = new URL(linkTab.origin);
      url.protocol = own.protocol, url.host = own.host;
      if (process.env.PAIR_ARGS) url.searchParams.set('args', process.env.PAIR_ARGS);
      t = await linkTab(url.href);
      guests.push(t);
      await until(`${t.name} boots from the link`, 180, () => t.evaluate('typeof pbModule !== "undefined" && !!pbModule._pbnet_lobby_json'));
      await t.evaluate(DEFINE_PB);
      say(`${t.name}: page address after the start: ${await t.evaluate('location.href')}`);
      await until(`${t.name}: the lobby from the link`, 120, async () => ['connecting', 'open'].includes((await json(t)).state) && (await t.focus()) !== '');
    } else {
      // the friend copies the code (the clipboard is the browser's, shared by the tabs), presses Ctrl+V on the page
      // (pb.js reads the clipboard into SDL's), then Online > Join > Paste Code > Join Game
      await t.key('v', true);
      await openOnline(t);
      await t.key('PageDown');
      await t.press('Paste Code');
      // the Code field shows the pasted link, shortened
      const field = await t.find('Code ');
      if (field.includes('(none)')) throw new Error(`${t.name}: Paste Code left the code empty`);
      say(`${t.name}: [${field}]`);
      await t.press('Join Game');
    }
    const answer = await until(`${t.name}'s answer`, 60, async () => (await json(t)).answer);
    say(`${t.name}: answer ${answer.length} chars`);
    await until(`${t.name}: Copy Answer enabled`, 20, async () => { await t.press('Copy Answer'); return (await t.clipboard()) === answer; });
    say(`${t.name}: Copy Answer put its answer on the clipboard`);
    await host.key('v', true);
    await host.press('Paste Answer');
    await sleep(1000);
    await host.shot(`pasted-${t.name}`);
    await until(`${t.name} is seated`, 40, async () => {
      const [h, g] = [await json(host), await json(t)];
      if (h.error) say(`a: lobby says: ${h.error}`);
      return g.room?.seats?.filter(Boolean).length === i + 2;
    });
    say(`${t.name}: seated`);
    const fresh = await until('the host\'s next code', 15, async () => { const c = (await json(host)).short; return c && !code.includes(c) && c; });
    say(`a: the next guest's code is a fresh offer (${fresh.slice(0, 16)}...)`);
    await host.find('Code rtco');
    await host.key('Enter');
    code = await until('the next invite link', 10, async () => { const c = await host.clipboard(); return c.includes(fresh) && c; });
  }
  for (const g of guests) {
    if (process.env.PAIR_DEBUG) {
      for (let i = 0; i < 4; i++) {
        const j = await json(g);
        say(`${g.name}: gen ${j.gen} state ${j.state} focus [${await g.focus()}]`);
        await g.key(i % 2 ? 'ArrowUp' : 'ArrowDown');
      }
    }
    // a press can get lost while the lobby redraws (four tabs: a few frames a second): again while it says Off
    await until(`${g.name}: ready`, 40, async () => {
      const j = await json(g);
      if (j.room?.ready?.includes(j.me)) return true;
      if ((await g.find('Ready')).endsWith('Off')) await g.press('Ready');
      await sleep(2000);
    });
  }
  await until('the start', 30, async () => {
    await host.press('Start Match');
    await sleep(1000);
    if ((await json(host)).state === 'started') return true;
    // "Can't Start" (not everyone ready yet): its OK
    await host.key('Enter');
  });
  say('a: started from the menu');
}

const DEFINE_PB = `window.__pb = {
    str(s) { return pbModule.stringToNewUTF8(s); },
    call(fn, ...strs) { const p = strs.map(s => typeof s === 'string' ? this.str(s) : s); const r = pbModule[fn](...p);
      p.forEach((x, i) => typeof strs[i] === 'string' && pbModule._free(x)); return r; },
    json() { const n = pbModule._pbnet_lobby_json(0, 0); const b = pbModule._malloc(n + 1); pbModule._pbnet_lobby_json(b, n + 1);
      const s = pbModule.UTF8ToString(b); pbModule._free(b); return JSON.parse(s); },
    why(fn, ...args) { const b = pbModule._malloc(256); const r = this.call(fn, ...args, b, 256); const w = pbModule.UTF8ToString(b);
      pbModule._free(b); return r ? '' : w || 'refused'; },
  }; true`;

// N98: guest b's connection drops mid-match; it comes back through the hold screens' buttons.
async function dropAndRedial(host, b) {
  const json = t => t.evaluate('__pb.json()');
  const recover = t => t.evaluate(`(() => { const n = pbModule._pbnet_recover_json(0, 0); const p = pbModule._malloc(n + 1);
    pbModule._pbnet_recover_json(p, n + 1); const s = pbModule.UTF8ToString(p); pbModule._free(p); return JSON.parse(s); })()`);
  // no fake pad presses now: its A would press the hold screens' first button (Keep Waiting)
  for (const t of peers) await t.evaluate('window.__pbMash = false, true');
  await sleep(1500);
  const frame = await b.evaluate('pbModule._pbnet_frame()');
  const closed = await b.evaluate('(__pbPcs.filter(pc => pc.connectionState === "connected").forEach(pc => pc.close()), __pbPcs.length)');
  say(`b: dropped its peer connections at frame ${frame} (${closed} made so far)`);
  await until('b: reconnecting', 30, async () => (await recover(b)).reconnecting);
  const answer = await until('b: its redial answer', 30, async () => (await json(b)).answer);
  say(`b: reconnecting, its answer ${answer.length} chars`);
  await until('b: Copy Answer on the hold screen', 20, async () => { await b.press('Copy Answer'); return (await b.clipboard()) === answer; });
  await b.shot('dropped');
  say('b: Copy Answer (hold screen) put its answer on the clipboard');
  await until('a: the waiting screen', 60, async () => (await recover(host)).waiting?.length > 0 && (await host.focus()) !== '');
  await host.shot('waiting');
  await host.key('v', true);
  await host.press('Paste Answer');
  await until('b: back', 60, async () => !(await recover(b)).reconnecting && !(await json(b)).answer);
  const back = await b.evaluate('pbModule._pbnet_frame()');
  say(`b: back in the match (frame ${back}); host log: ${host.lines.filter(l => l.includes('rejoin of')).slice(-1)[0] ?? 'no rejoin line'}`);
  // the hold screens close by themselves; any left open would take the pad's presses
  for (const t of peers) {
    for (let i = 0; i < 3 && (await t.focus()) !== ''; i++) await t.key('Escape');
  }
  for (const t of peers) await t.evaluate('window.__pbMash = true');
  return back;
}

let peers = [];
try {
  const linkGuest = menu && nTabs >= 3 ? 2 : -1;
  for (const [i, n] of names.entries()) if (i !== linkGuest) peers.push(await tab(n, pages[i], origins[i]));
  for (const t of peers) {
    await until(`${t.name} boots`, 120, () => t.evaluate('typeof pbModule !== "undefined" && !!pbModule._pbnet_lobby_json'));
    // __pb was defined before the module existed: define it again now
    await t.evaluate(DEFINE_PB);
  }
  // the title screen first (bootDll done), as a player opening the Online menu would be
  await sleep(25000);
  for (const t of peers) {
    await t.evaluate('__pbConnect(), true');
    await t.shot('title');
  }
  const [host, ...guests] = peers;
  if (desktop && process.env.PAIR_DESKTOP_GUEST) {
    await lobbyForDesktop(peers[0]);
  } else if (desktop) {
    await lobbyWithDesktop(peers[0]);
  } else if (menu) {
    const linkTab = linkGuest < 0 ? null : Object.assign(async url => {
      const t = await tab(names[linkGuest], pages[linkGuest], origins[linkGuest], url);
      // the guest list keeps the tab order: c before d
      peers.splice(linkGuest, 0, t);
      return t;
    }, { origin: origins[linkGuest] });
    await lobbyByMenu(host, guests, linkTab);
    // the started match hides the lobby; the menu bar the menu opened goes too
    await sleep(2000);
    for (const t of peers) {
      for (let i = 0; i < 3 && (await t.focus()) !== ''; i++) await t.key('Escape');
      await t.evaluate('__pbConnect(), true');
    }
  } else {
    await lobbyByAbi(host, guests);
  }
  for (const t of peers) writeFileSync(join(out, `lobby-${t.name}.json`), JSON.stringify(await t.evaluate('__pb.json()'), null, 2));
  for (const t of peers) {
    const sdps = await t.evaluate('__pbPcs.map(pc => pc.localDescription?.sdp ?? "")');
    const kinds = ['host', 'srflx', 'relay'].map(k => `${k} ${sdps.join('').split(`typ ${k}`).length - 1}`);
    say(`${t.name}: ${sdps.length} peer connections, candidates: ${kinds.join(', ')}`);
  }
  if (process.env.PAIR_MASH !== '0') for (const t of peers) await t.evaluate('window.__pbMash = true');
  const every = +(process.env.SHOT_EVERY ?? 30);
  const dropAt = +(process.env.PAIR_DROP ?? 0);
  let frames = peers.map(() => 0);
  let backAt = dropAt && menu ? -1 : 0;
  // frames the host advanced since the redial (a reload starts the count of frames again)
  let afterBack = 0;
  for (let s = 1; s <= +secs; s++) {
    await sleep(1000);
    const before = frames[0];
    frames = [];
    for (const t of peers) frames.push(await t.evaluate('pbModule._pbnet_frame()'));
    if (backAt > 0) afterBack += frames[0] >= before ? frames[0] - before : frames[0];
    if (s % 10 === 0) {
      const wait = [];
      for (const t of peers) wait.push(await t.evaluate('pbModule._pbnet_waiting_on()'));
      say(`lockstep frames ${peers.map((t, i) => `${t.name}=${frames[i]}`).join(' ')} (waiting on ${peers.map((t, i) => `${t.name}=${wait[i]}`).join(' ')})`);
    }
    if (s % every === 0) for (const t of peers) await t.shot(`match-${String(s).padStart(3, '0')}`);
    if (dropAt && menu && s === dropAt) {
      backAt = await dropAndRedial(host, peers[1]).catch(e => (say(`drop: ${e.message}`), -1));
      frames[0] = backAt > 0 ? backAt : frames[0];
    }
    // START 300 frames into instDll (its intro is over), again 700 frames in if it still shows; whatever the frame rate
    for (const [i, t] of peers.entries()) {
      if (t.mg.at < 0 || frames[i] < t.mg.at) continue;
      if ((t.mg.stage === 0 && frames[i] >= t.mg.at + 300) || (t.mg.stage === 1 && frames[i] >= t.mg.at + 700)) {
        t.mg.stage++;
        await t.evaluate('window.__pbStarts = [Date.now() + 1000], true');
        await t.shot(`mg-${String(s).padStart(3, '0')}`);
      }
    }
  }
  for (const t of peers) await t.shot('last');
  for (const path of (process.env.DUMP_FILES ?? '').split(' ').filter(Boolean)) for (const t of peers) await t.dump(path).catch(e => say(String(e)));
  // pbnet's desync input logs (the current directory without the Online menu, which sets the user dir)
  for (const t of peers) {
    for (const dir of ['/', '/libsdl/MarioPartyRD/Party Board/']) {
      const logs = await t.evaluate(`(() => { try { return pbModule.FS.readdir(${JSON.stringify(dir)}).filter(n => n.startsWith('desync-')); } catch (e) { return []; } })()`).catch(() => []);
      for (const l of logs ?? []) await t.dump(`${dir}${l}`).catch(e => say(String(e)));
    }
  }
  const desyncs = peers.map(t => t.lines.filter(l => /desync|out of sync|mismatch/i.test(l)).length);
  const startedLines = peers.map(t => t.lines.some(l => l.includes('netplay: lobby started')));
  const redialOk = backAt === 0 || (backAt > 0 && afterBack >= 300);
  // N97: the match left for the menu (modeseldll, mentDll, bootDll) without the party's results
  const menuEnds = peers.map(t => t.lines.filter(l => /netplay: match over: overlay \d+ starts/.test(l)).length);
  const reached = peers.map(t => {
    const points = t.lines.map(l => l.match(/netplay: resync point at frame (\d+): .*turn (\d+)\//)).filter(Boolean);
    const turns = points.map(m => +m[2]);
    // minigame overlays: m300Dll (4) to m463Dll (69) in include/ovl_table.h
    const games = t.lines.map(l => l.match(/Start New OVL (\d+) /)).filter(m => m && +m[1] >= 4 && +m[1] <= 69).length;
    const killed = t.lines.filter(l => l.includes('menu processes')).map(l => l.match(/killed (\d+)/)?.[1]).join(',');
    // a match that stopped moving on (e.g. a human seat in the board lottery's stick games: this pad has no stick)
    const last = points.length ? `, last turn/minigame point at frame ${points[points.length - 1][1]}` : '';
    return `${t.name}: turn ${turns.length ? Math.max(...turns) : 0}, ${games} minigames started${last}, menu processes killed ${killed || '-'}`;
  });
  say(`reached: ${reached.join('; ')}${menuEnds.some(n => n) ? `; LEFT FOR THE MENU: ${peers.map((t, i) => `${t.name}=${menuEnds[i]}`).join(' ')}` : ''}`);
  say(`started: ${peers.map((t, i) => `${t.name}=${startedLines[i]}`).join(' ')}; desync lines: ${peers.map((t, i) => `${t.name}=${desyncs[i]}`).join(' ')}; ` +
    `frames ${peers.map((t, i) => `${t.name}=${frames[i]}`).join(' ')}${dropAt ? `; redial ${redialOk ? 'ok' : 'FAILED'} (${afterBack} frames after it)` : ''}`);
  verdict = startedLines.every(Boolean) && frames.every(f => f > 600) && desyncs.every(n => n === 0) && menuEnds.every(n => n === 0) && redialOk ? 0 : 1;
  say(verdict === 0 ? 'PASS' : 'FAIL');
} catch (e) {
  say(`error: ${e.stack ?? e}`);
  for (const t of peers) await t.shot('error').catch(() => {});
}
finish();
