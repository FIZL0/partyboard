// N92: two browser peers in one headless Chromium (tools/web.sh's build/web): tab A hosts a lobby, tab B joins it
// over WebRTC (net/src/rtc.rs), the codes go both ways through this script as a player would copy them, and the
// match runs in lockstep. As tools/web_check.mjs (server, GPU flags, console capture), with two tabs on two origins
// (127.0.0.1 and localhost: separate OPFS and IndexedDB, so separate discs in storage and user dirs).
//
// The lobby is driven through the game's own C ABI (exported by cmake/web.cmake: pbnet_lobby_host / _join /
// _answer / _ready / _start / _json), which is what the Online menu calls; the match then starts as from the menu
// (netplay_lobby_poll). Each tab gets a fake standard gamepad (navigator.getGamepads) that taps A once the match
// runs, so both human seats play their turns (N104: and moves its stick in the board lottery's stick games and m444's
// plunger), and one START 300 frames after a minigame's instructions start (the console's "resync point ...
// minigame", N52's instDll point), another at 700 frames if they still show.
// N126: in a minigame that has run 2400 frames the pad sweeps B/X/Y, the triggers and the stick too (PAIR_SWEEP), and
// the summary lists minigames that ran over 4 minutes (m427 and m432 run to their 5-minute cap unplayed).
// N113: START at every instDll start ("Start New OVL 3"), not only at a minigame point: a battle/Bowser minigame
// (mid-turn) and an instDll resumed after a reload print no point, and stood at their instructions for ever (N102's
// d1/d2/m1). A --desync-at flip inside instDll shows only once the minigame runs (N64: instDll is not hash-checked);
// the summary says whether each peer's flip was followed by a desync.
// N97: the START that sent N92's matches to the menu from instDll was not instDll's: the title's BootExec (bootDll)
// ran on under the match (N99), and its "press START" called modeseldll. A run whose match ends at the menu without
// the party's results now fails, and the summary says how far the match got (turn, minigames started).
//
// N98: up to four tabs (PAIR_TABS; the host relays guest to guest), the lobby through the Online menu instead
// (PAIR_MENU=1: real key presses over CDP, the codes through the page's clipboard bridge: Ctrl+V -> pb.js ->
// pb_web_paste, copies -> navigator.clipboard; the browser's clipboard permission granted over CDP), and a guest's
// mid-match drop and redial (PAIR_DROP).
//
// N139: a WebRTC host with the relay on puts its offers under a short code on relay.pkarr.org (net/src/signal.rs):
// the guests join with those 4 symbols (or the ?code= link) and nothing goes back by hand. PAIR_RELAY (the ABI
// lobby's relay setting, default off: offer and answer codes as before; n0: the short code); the menu's profile is n0
// already. PAIR_BLOCK_RELAY=1 blocks relay.pkarr.org in every tab (the fallback: long codes and answers again).
// PAIR_FREEZE=S freezes guest b's page (CDP Page.setWebLifecycleState, as a phone switching apps) for S seconds once
// its answer is on the code server, then wakes it: it must still be seated.
//
// N139a: PAIR_NEARBY=1: the guests find the host in their LAN list (records under the tabs' public address,
// net/src/nearby.rs) and join it from there, no code copied. ABI lobby: `pbnet_lan_browse` until the host's short code
// is listed; the host then leaves, b times how long its lobby stays listed, the host opens a new lobby, and the guests
// join the new code from the list. With PAIR_MENU: Online > LAN, the game's button. Needs the relay on (PAIR_RELAY=n0).
//
// Usage: node tools/web_pair.mjs DISC OUT [SECS]
//   SECS: how long the match runs after the start (default 120). OUT gets a.txt / b.txt (each tab's console),
//   shots a-*.png / b-*.png, lobby-*.json, and pair.txt (the run's log: codes, frames, waits, verdict).
// Env: WEB_ROOT (the build to serve, default build/web), WEB_PORT (8788: not web_check.mjs's, which other slots may be running), CHROME, CHROME_FLAGS, SHOT_EVERY (30), PAIR_CPUS ("2,3": seats the host gives
//      the computer, default both free seats), PAIR_MASH (0: no fake gamepad presses), PAIR_ARGS (the game's command
//      line in both tabs, e.g. '--hash-log /tmp/h.log'), PAIR_ARGS_B (N102: tab b's instead, e.g. with a
//      '--desync-at F'), DUMP_FILES ("/tmp/h.log": copied out of each tab's FS into
//      OUT as a-h.log / b-h.log at the end), PAIR_KEEP_PROFILE (keep OUT/profile, removed otherwise), PAIR_SEED (the match seed, e.g. 0x18E2A0AF4FE4E301; default from the clock).
//      N126: PAIR_SWEEP (game frames a minigame runs before the fake pad's sweep, default 2400; 0: no sweep).
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
import { appendFileSync, createReadStream, existsSync, mkdirSync, openSync, readFileSync, rmSync, statSync, writeFileSync } from 'node:fs';
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
const nTabs = Math.min(Math.max(+(process.env.PAIR_TABS ?? 2), 2), 4);
const names = ['a', 'b', 'c', 'd'].slice(0, nTabs);
const menu = process.env.PAIR_MENU === '1';
// N102: PAIR_ARGS_B is tab b's command line instead of PAIR_ARGS (e.g. its own --desync-at)
const tabArgs = name => (name === 'b' ? process.env.PAIR_ARGS_B : undefined) ?? process.env.PAIR_ARGS;
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
  // (N126: through finish, which also kills the Chromium spawned by then: process.exit alone left it running)
  server.on('error', e => { console.error(`web_pair: ${e.message}`); finish(); });
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
    // N104: A held back while m444's plunger pulls
    const pull = !!window.__pbMash && plungerPull();
    const down = !!window.__pbMash && now % 500 < 80;
    // N126: a minigame still running long after it began: every button, both triggers and the stick in turn
    const sweep = !!window.__pbMash && !pull && !rest ? minigameSweep() : null;
    const buttons = sweep ? [[0, sweep.a], [1, sweep.b], [2, sweep.x], [3, sweep.y], [6, sweep.l], [7, sweep.r], [9, start]]
      : [[0, down && !rest && !pull], [1, false], [2, false], [3, false], [6, false], [7, false], [9, start]];
    for (const [b, on] of buttons) {
      if (pad.buttons[b].pressed !== on) {
        pad.buttons[b].pressed = on; pad.buttons[b].value = on ? 1 : 0; pad.timestamp++;
      }
    }
    // N104: the board lottery's stick games (ball spin, ticket scratch) and m444's plunger read only the stick
    const [x, y] = pull ? [0, 120 / 127] : sweep ? sweep.stick : window.__pbMash ? lotteryStick() : [0, 0];
    if (pad.axes[0] !== x || pad.axes[1] !== y) pad.axes[0] = x, pad.axes[1] = y, pad.timestamp++;
  }, 8);
  // N104: while a human current player is in a lottery stick game (pb_web_lottery_stick), the stick for its game
  // frame t (pbnet_frame since the game began; the pad is read once a frame, so this is per frame at any frame rate):
  // netplay.c's lottery_stick_at (--script's lottery-stick on), as standard gamepad axes (x right, y down; 120/127).
  // Ball: 20 degrees a frame clockwise (only rising steps under 30 count). Scratch: wait out the hand's 64-frame
  // descent, left 30, up 15, then 13 rows across (26 frames) and one frame down, over again until it counts.
  const SPIN = [[0, 120], [41, 113], [77, 92], [104, 60], [118, 21], [118, -21], [104, -60], [77, -92], [41, -113],
    [0, -120], [-41, -113], [-77, -92], [-104, -60], [-118, -21], [-118, 21], [-104, 60], [-77, 92], [-41, 113]];
  const lot = { game: 0, from: 0 };
  window.__pbStick = [];
  const lotteryStick = () => {
    const m = window.pbModule;
    if (!m?._pb_web_lottery_stick || !m._pbnet_frame) return [0, 0];
    const game = m._pb_web_lottery_stick(), frame = m._pbnet_frame();
    // a new game, or the frame count started over (a resync reload)
    if (game !== lot.game || frame < lot.from) {
      lot.game = game, lot.from = frame;
      if (game) window.__pbStick.push(\`\${game === 1 ? 'ball game' : 'scratch'} at frame \${frame}\`);
    }
    if (!game) return [0, 0];
    const t = frame - lot.from;
    let sx = 0, sy = 0;
    if (game === 1) {
      [sx, sy] = SPIN[t % 18];
    } else if (t >= 64) {
      let c = (t - 64) % (30 + 15 + 13 * 27);
      if (c < 30) sx = -120;
      else if (c < 45) sy = 120;
      else if ((c -= 45) % 27 === 26) sy = -120;
      else sx = Math.floor(c / 27) % 2 === 0 ? 120 : -120;
    }
    return [sx / 127, -sy / 127];
  };
  // N104: m444's plunger (pb_web_plunger_pad: it waits on a pad): stick down and no A for its first 90 frames (a full
  // pull: a short one can leave the ball bouncing in the lane for ever), then A shoots. As --script's plunger-stick on.
  const plunger = { pad: -1, from: 0 };
  const plungerPull = () => {
    const m = window.pbModule;
    if (!m?._pb_web_plunger_pad || !m._pbnet_frame) return false;
    const pad = m._pb_web_plunger_pad(), frame = m._pbnet_frame();
    if (pad !== plunger.pad || frame < plunger.from) {
      plunger.pad = pad, plunger.from = frame;
      if (pad >= 0) window.__pbStick.push(\`plunger (pad \${pad}) at frame \${frame}\`);
    }
    return pad >= 0 && frame - plunger.from < 90;
  };
  // N126: minigames A taps can't play stood a human seat still until their own 5-minute cap (N104 run 3: m432,
  // both climbers at the bottom; run 4: m427's 2v2 boat race, a boat standing at the start). The harness calls
  // __pbMinigame(overlay) at every overlay start (0: not a minigame). Once one has run 2400 game frames (a timed
  // minigame's clock and intro are mostly over by then), the pad plays what such minigames wait on, by game frame, in
  // three 480-frame phases, over again (PAIR_SWEEP: another wait, 0: no sweep):
  //   forward: stick up, A and B take turns, a press every 4 frames (m427's oars: A left, B right; A also jumps);
  //   no jump: stick up, B, X and Y take turns, a press every 4 frames, no A (m432's B/X/Y holds count presses only on the
  //            ground and drop a player who doesn't press for 20 frames);
  //   around:  the stick round the 8 directions, 60 frames each, A, B, X and Y taking turns the same way.
  // L and R take turns all along, 20 frames each (m432's crank counts an R pull followed by an L one). A frame count
  // that starts over (a resync reload) starts the wait over.
  // (GC axes, y up: 120 straight, 85 each on a diagonal)
  const STICK8 = [[0, 120], [85, 85], [120, 0], [85, -85], [0, -120], [-85, -85], [-120, 0], [-85, 85]];
  const mgame = { ovl: 0, from: -1, swept: false };
  window.__pbMinigame = ovl => { mgame.ovl = ovl, mgame.from = -1, mgame.swept = false; };
  const minigameSweep = () => {
    const m = window.pbModule;
    const after = window.__pbSweepAfter ?? 2400;
    if (!mgame.ovl || !after || !m?._pbnet_frame) return null;
    const frame = m._pbnet_frame();
    if (mgame.from < 0 || frame < mgame.from) mgame.from = frame, mgame.swept = false;
    const t = frame - mgame.from - after;
    if (t < 0) return null;
    if (!mgame.swept) mgame.swept = true, window.__pbStick.push(\`minigame sweep (overlay \${mgame.ovl}) at frame \${frame}\`);
    const phase = Math.floor(t / 480) % 3;
    // which button of a set is down this frame: each held 3 frames, then a frame up
    const turn = (n, period) => (t % period) % 4 < 3 ? Math.floor((t % period) / 4) % n : -1;
    const k = phase === 0 ? turn(2, 8) : phase === 1 ? turn(3, 12) : turn(4, 16);
    const set = phase === 0 ? ['a', 'b'] : phase === 1 ? ['b', 'x', 'y'] : ['a', 'b', 'x', 'y'];
    const [sx, sy] = phase === 2 ? STICK8[Math.floor(t / 60) % 8] : [0, 120];
    const pad = { a: false, b: false, x: false, y: false, l: t % 40 < 20, r: t % 40 >= 20, stick: [sx / 127, -sy / 127] };
    if (k >= 0) pad[set[k]] = true;
    return pad;
  };
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
  // the frame instDll started at (-1: none pending; -2: started, the main loop reads the frame) and how many STARTs it
  // got
  const mg = { at: -1, stage: 0 };
  // N126: every overlay start (number, wall-clock ms), for the summary's long minigames
  const ovls = [];
  ws.onmessage = ({ data }) => {
    const m = JSON.parse(data);
    if (m.id && waiting.has(m.id)) waiting.get(m.id)(m), waiting.delete(m.id);
    let line;
    if (m.method === 'Runtime.consoleAPICalled') line = `${m.params.type}: ${m.params.args.map(a => a.value ?? a.description).join(' ')}`;
    if (m.method === 'Runtime.exceptionThrown') line = `exception: ${m.params.exceptionDetails.exception?.description ?? m.params.exceptionDetails.text}`;
    if (m.method === 'Log.entryAdded') line = `log ${m.params.entry.level}: ${m.params.entry.text}`;
    if (line) {
      const js = expression => ws.send(JSON.stringify({ id: 999990, method: 'Runtime.evaluate', params: { expression } }));
      // the minigame's instructions: START once they show (the main loop times it in game frames). Any instDll start
      // (N113: battle/Bowser minigames and a resumed instDll print no minigame point; N104: a board event's mid-turn
      // minigame, e.g. N104 run 2's "Paths of Peril" at turn 3); a minigame point, printed just
      // after it, gives the exact frame
      if (line.includes('Start New OVL 3 ')) mg.at = -2, mg.stage = 0;
      const point = line.match(/netplay: resync point at frame (\d+): minigame/);
      if (point) mg.at = +point[1], mg.stage = 0;
      // the minigame started: no more START
      if (line.includes('Start New OVL') && !line.includes('Start New OVL 3 ')) mg.at = -1, js('window.__pbStarts = []');
      // N126: the overlay that runs, for the pad's sweep in minigames (m300Dll (4) to m463Dll (69), include/ovl_table.h)
      const ovl = line.match(/Start New OVL (\d+) /);
      if (ovl) js(`window.__pbMinigame?.(${+ovl[1] >= 4 && +ovl[1] <= 69 ? +ovl[1] : 0})`), ovls.push([+ovl[1], Date.now()]);
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
  // N139: the fallback: the short codes' relay unreachable from this tab
  if (process.env.PAIR_BLOCK_RELAY === '1') {
    await send('Network.enable');
    await send('Network.setBlockedURLs', { urls: ['*relay.pkarr.org*'] });
  }
  await send('Page.addScriptToEvaluateOnNewDocument', { source: PAD });
  // N98: the clipboard for the menu's copies and Ctrl+V (as a player who allowed it), and every tab "focused" (the
  // async clipboard API needs a focused document; only one tab of a window would be)
  await send('Browser.grantPermissions', { permissions: ['clipboardReadWrite', 'clipboardSanitizedWrite'], origin });
  await send('Emulation.setFocusEmulationEnabled', { enabled: true });
  const line = tabArgs(name);
  const args = line ? `?args=${encodeURIComponent(line)}` : '';
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
  return { name, origin, send, evaluate, lines, shot, dump, mg, ovls, key, focus, find, press, clipboard };
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
  const setup = name => JSON.stringify({ build: 'web-pair', disc: 'GMPE01-01', relay: process.env.PAIR_RELAY ?? 'off', short: true,
    players: [{ name, key: `key-${name}` }] });
  const hostLobby = async () => {
    if (!await host.evaluate(`__pb.call('_pbnet_lobby_host', ${JSON.stringify(setup('Host'))})`)) throw new Error('a: host refused');
    // N139: with the relay on, the short code once it is online (the offer shows first)
    let polls = 0;
    return until('the host\'s code', 60, async () => {
      const j = await host.evaluate('__pb.json()');
      if (++polls % 40 === 0) say(`a: lobby ${j.state}, short ${JSON.stringify(j.short?.slice(0, 12))}, error ${JSON.stringify(j.error)}`);
      const c = j.short;
      return c && (!wantShort || SHORT.test(c)) && c;
    });
  };
  let code = await hostLobby();
  say(`a: hosting, code ${code.length} chars: ${code}`);
  if (nearby) {
    // N139a: listed for b's network, gone once the host leaves, the next lobby listed in its place
    const t0 = Date.now();
    await until(`b: ${code} in the LAN list`, 120, async () => (await lanList(guests[0], setup('Guest1'))).games?.some(g => g.code === code));
    say(`b: the LAN list has ${code} ${((Date.now() - t0) / 1000).toFixed(1)} s after the code showed: ${JSON.stringify((await lanList(guests[0], setup('Guest1'))).games)}`);
    await host.evaluate('pbModule._pbnet_lobby_leave(), true');
    const t1 = Date.now();
    await until(`b: ${code} gone from the LAN list`, 75, async () => !(await lanList(guests[0], setup('Guest1'))).games?.some(g => g.code === code));
    say(`b: ${code} left the LAN list ${((Date.now() - t1) / 1000).toFixed(1)} s after the host left`);
    code = await hostLobby();
    say(`a: hosting again, code ${code}`);
  }
  for (const [i, g] of guests.entries()) {
    if (nearby) {
      const t0 = Date.now();
      const game = await until(`${g.name}: ${code} in the LAN list`, 120, async () => (await lanList(g, setup(`Guest${i + 1}`))).games?.find(x => x.code === code));
      say(`${g.name}: joins from the LAN list ${((Date.now() - t0) / 1000).toFixed(1)} s later: ${JSON.stringify(game)}`);
    }
    if (!await g.evaluate(`__pb.call('_pbnet_lobby_join', ${JSON.stringify(setup(`Guest${i + 1}`))}, ${JSON.stringify(code)})`)) throw new Error(`${g.name}: join refused`);
    if (SHORT.test(code)) {
      await joinedByShort(host, g, i, code, t => t.evaluate('__pb.json()'));
      continue;
    }
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

// N139a: the LAN list (`pbnet_lan_browse`) of tab `t`
const nearby = process.env.PAIR_NEARBY === '1';
const lanList = (t, setup) => t.evaluate(`(() => { const s = __pb.str(${JSON.stringify(setup)}); const n = pbModule._pbnet_lan_browse(s, 0, 0);
  const b = pbModule._malloc(n + 1); pbModule._pbnet_lan_browse(s, b, n + 1); const j = pbModule.UTF8ToString(b); pbModule._free(b);
  pbModule._free(s); return JSON.parse(j) ?? {}; })()`);

// N139: short codes: the 4 symbols, and whether the run waits for one (the relay on and not blocked)
const SHORT = /^[2-9A-HJ-NP-Z]{4}$/;
const wantShort = process.env.PAIR_BLOCK_RELAY !== '1' && (menu || (process.env.PAIR_RELAY ?? 'off') !== 'off');

// N139: guest `g` (the i-th) joined by the short code: no answer to copy; seated by itself. Guest b's page is frozen
// for PAIR_FREEZE seconds once its answer is on the code server. The host's code stays the same.
async function joinedByShort(host, g, i, code, json) {
  const freeze = +(process.env.PAIR_FREEZE ?? 0);
  if (freeze && g.name === 'b') {
    await until('b: its answer on the code server', 60, async () => g.lines.some(l => l.includes('answer put on the code server')));
    await g.send('Page.setWebLifecycleState', { state: 'frozen' });
    say(`b: answer on the code server; page frozen for ${freeze} s`);
    await sleep(freeze * 1000);
    say(`a: lobby while b was frozen: ${JSON.stringify((await json(host)).error ?? '')}`);
    await g.send('Page.setWebLifecycleState', { state: 'active' });
    say('b: page active again');
  }
  await until(`${g.name} is seated`, 90, async () => (await json(g)).room?.seats?.filter(Boolean).length === i + 2);
  const j = await json(g);
  if (j.answer) throw new Error(`${g.name}: shows an answer to send back after a short-code join`);
  say(`${g.name}: seated by the short code alone (nothing sent back)`);
  const now = (await json(host)).short;
  if (now !== code) throw new Error(`a: the code changed from ${code} to ${now}`);
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
  const shown = await until('a: hosting', 90, async () => { const c = (await json(host)).short; return c && (!wantShort || SHORT.test(c)) && c; });
  await host.find(`Code ${shown.slice(0, 4)}`);
  await host.key('Enter');
  const link = await until('the invite link on the clipboard', 10, async () => { const c = await host.clipboard(); return c.includes(`?code=${shown.slice(0, 8)}`) && c; });
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
    } else if (nearby) {
      // N139a: Online > LAN, the host's game in the list (its button says the open seats), pressed
      await openOnline(t);
      await t.key('PageDown');
      await t.key('PageDown');
      const t0 = Date.now();
      const f = await until(`${t.name}: the game in the LAN tab`, 120, async () => {
        for (let k = 0; k < 6; k++) {
          const f = await t.focus();
          if (/seats? open/.test(f)) return f;
          await t.key('ArrowDown');
        }
      });
      await t.shot('lan');
      say(`${t.name}: LAN tab shows [${f}] after ${((Date.now() - t0) / 1000).toFixed(1)} s`);
      await t.key('Enter');
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
    if (SHORT.test(shown)) {
      await joinedByShort(host, t, i, shown, json);
      continue;
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
  // N139: joined by a short code, the redial's answer goes through the code server: nothing to copy or paste
  if (wantShort) {
    await b.shot('dropped');
    await until('b: back by the short code', 90, async () => !(await recover(b)).reconnecting);
    if ((await json(b)).answer) throw new Error('b: shows an answer to send back after a short-code redial');
    const back = await b.evaluate('pbModule._pbnet_frame()');
    say(`b: back in the match by the short code (frame ${back}); host log: ${host.lines.filter(l => l.includes('rejoin of') || l.includes('by the short code')).slice(-1)[0] ?? 'no rejoin line'}`);
    for (const t of peers) {
      for (let i = 0; i < 3 && (await t.focus()) !== ''; i++) await t.key('Escape');
    }
    for (const t of peers) await t.evaluate('window.__pbMash = true');
    return back;
  }
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
  if (menu) {
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
  if (process.env.PAIR_SWEEP) for (const t of peers) await t.evaluate(`window.__pbSweepAfter = ${+process.env.PAIR_SWEEP}, true`);
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
      // N104: the stick games the pads began since (lottery, plunger)
      for (const t of peers) {
        const games = (await t.evaluate('window.__pbStick').catch(() => null)) ?? [];
        for (const g of games.slice(t.sticks ?? 0)) say(`${t.name}: stick game: ${g}`);
        t.sticks = games.length;
      }
    }
    if (s % every === 0) for (const t of peers) await t.shot(`match-${String(s).padStart(3, '0')}`);
    if (dropAt && menu && s === dropAt) {
      backAt = await dropAndRedial(host, peers[1]).catch(e => (say(`drop: ${e.message}`), -1));
      frames[0] = backAt > 0 ? backAt : frames[0];
    }
    // START 300 frames into instDll (its intro is over), again 700 frames in if it still shows; whatever the frame rate
    for (const [i, t] of peers.entries()) {
      if (t.mg.at === -2) t.mg.at = frames[i];
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
  // N104: the stick games each tab's pad played (lottery, plunger)
  for (const t of peers) {
    const games = await t.evaluate('window.__pbStick').catch(() => null);
    if (games?.length) say(`${t.name}: stick games: ${games.join(', ')}`);
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
    // a match that stopped moving on (as a human seat in the board lottery's stick games did before N104)
    const last = points.length ? `, last turn/minigame point at frame ${points[points.length - 1][1]}` : '';
    return `${t.name}: turn ${turns.length ? Math.max(...turns) : 0}, ${games} minigames started${last}, menu processes killed ${killed || '-'}`;
  });
  // N113: a peer's --desync-at flip and the first desync any peer saw from its frame on (a flip in instDll shows once
  // the minigame runs; only the peer that compares the hashes, often the host, logs it)
  const flips = peers.map(t => {
    const flip = t.lines.map(l => l.match(/netplay: --desync-at (\d+)/)).find(Boolean);
    if (!flip) return null;
    const seen = peers.map(p => [p.name, p.lines.map(l => l.match(/netplay: desync at frame (\d+)/)).find(m => m && +m[1] >= +flip[1])])
      .filter(([, m]) => m).sort((x, y) => x[1][1] - y[1][1])[0];
    return `${t.name}: flip at ${flip[1]}, ${seen ? `desync at frame ${seen[1][1]} (${seen[0]})` : 'NO DESYNC after it'}`;
  }).filter(Boolean);
  if (flips.length) say(`desync-at: ${flips.join('; ')}`);
  // N126: minigames that ran over 4 minutes (m427 and m432 stop at their 5-minute cap when nobody plays them), and the
  // one still running at the end
  const long = peers.map(t => {
    const runs = t.ovls.map(([o, at], i) => [o, ((t.ovls[i + 1]?.[1] ?? Date.now()) - at) / 1000, !t.ovls[i + 1]]);
    const slow = runs.filter(([o, s]) => o >= 4 && o <= 69 && s > 240).map(([o, s, open]) => `o${o} ${Math.round(s)} s${open ? ' (still running)' : ''}`);
    return slow.length ? `${t.name}: ${slow.join(', ')}` : null;
  }).filter(Boolean);
  if (long.length) say(`minigames over 4 minutes: ${long.join('; ')}`);
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
