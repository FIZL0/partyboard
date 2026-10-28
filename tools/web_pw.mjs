// N93: drives the browser build (build/web) in Chromium, Firefox or WebKit through Playwright, to check the page on
// more than tools/web_check.mjs's headless Chromium: a persistent profile (OPFS disc, IndexedDB saves) kept across
// reloads and runs, touch (Chromium: CDP touch events, so holds and drags too), an emulated gamepad, keyboard,
// viewport changes (rotation), and a page's memory.
//
// Usage: node tools/web_pw.mjs BROWSER DISC OUT SECS [QUERY]
//   BROWSER chromium | firefox | webkit
//   DISC    the disc image, or '-' (the profile OUT/profile-BROWSER already has one in OPFS: QUERY wants autoplay=1)
//   QUERY   the page's query (see platforms/web/pb.js)
// Env: PLAYWRIGHT (the playwright package dir, if `import 'playwright'` doesn't find it), PW_EXE (the browser
//      executable), PW_HEADED=1, PW_PROFILE (profile dir), WEB_PORT (8788), WEB_FILES (as web_check.mjs),
//      PW_MOBILE=WxH (Chromium: a phone viewport with touch and a mobile UA), SHOT_EVERY (10), STOP_ON (console
//      substring that ends the run), PW_STEPS: ';'-separated "SECS:ACTION[:ARG]" steps:
//        key:Enter        press and release a key (Playwright key names: Enter, ArrowDown, KeyX, F1 ...)
//        hold:KeyX:MS     hold a key for MS milliseconds
//        tap:X,Y          a touch tap at X,Y (CSS px)        touch:X,Y:MS  a held touch (Chromium)
//        drag:X,Y,X2,Y2:MS  a touch moved from X,Y to X2,Y2 over MS (Chromium)
//        pad              connect an emulated standard-mapping gamepad   padbtn:N:MS  hold its button N
//        padaxis:I,V:MS   move its axis I to V for MS
//        size:W,H         set the viewport (rotation)        reload        reload the page
//        click:SELECTOR   click a page element               eval:JS       evaluate JS in the page, logged
//        pick:PATH        choose PATH in the page's disc picker (DISC '-' and a step before it changed the page)
//        evalfile:PATH    evaluate the JS in PATH (steps are split at ';')
//        shot:NAME        screenshot OUT/NAME.png            mem           log the page's memory use (Chromium)
//        dump:PATH        copy PATH out of the game's FS into OUT
import { createServer } from 'node:http';
import { createRequire } from 'node:module';
import { appendFileSync, createReadStream, existsSync, mkdirSync, readFileSync, statSync, writeFileSync } from 'node:fs';
import { basename, extname, join, resolve } from 'node:path';

const [browserName, disc, out, secs = '60', query = ''] = process.argv.slice(2);
if (!browserName || !disc || !out) {
  console.error('usage: node tools/web_pw.mjs chromium|firefox|webkit DISC OUT [SECS] [QUERY]');
  process.exit(2);
}
const pw = process.env.PLAYWRIGHT ? createRequire(import.meta.url)(process.env.PLAYWRIGHT) : await import('playwright');
mkdirSync(out, { recursive: true });
const root = resolve(new URL('..', import.meta.url).pathname, 'build/web');
const webPort = +(process.env.WEB_PORT ?? 8788);
const extra = new Map((process.env.WEB_FILES ?? '').split(' ').filter(Boolean).map(f => [basename(f), resolve(f)]));
const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.png': 'image/png' };
const sleep = ms => new Promise(r => setTimeout(r, ms));
const t0 = Date.now();
const stamp = () => `${((Date.now() - t0) / 1000).toFixed(1)}s`;
writeFileSync(join(out, 'console.txt'), '');
const log = line => appendFileSync(join(out, 'console.txt'), `[${stamp()}] ${line}\n`);

const server = createServer((req, res) => {
  const path = decodeURIComponent(new URL(req.url, 'http://x').pathname);
  const file = path.startsWith('/test/') ? extra.get(path.slice(6)) : join(root, path === '/' ? 'index.html' : path);
  if (!file || !file.startsWith(path.startsWith('/test/') ? '/' : root) || !existsSync(file) || !statSync(file).isFile()) {
    res.writeHead(404).end();
    return;
  }
  res.writeHead(200, { 'Content-Type': types[extname(file)] ?? 'application/octet-stream', 'Cache-Control': 'no-store' });
  createReadStream(file).pipe(res);
}).listen(webPort, '127.0.0.1');

const launch = {
  headless: !process.env.PW_HEADED,
  executablePath: process.env.PW_EXE,
  viewport: { width: 1280, height: 960 },
};
if (browserName === 'chromium') {
  // the machine's GPU (as web_check.mjs): headless SwiftShader loses the WebGPU canvas
  launch.args = ['--enable-unsafe-webgpu', '--ignore-gpu-blocklist', '--use-angle=vulkan', '--enable-features=Vulkan',
    '--use-vulkan=native', '--ozone-platform=headless', '--enable-precise-memory-info'];
}
if (browserName === 'firefox') {
  // Firefox ships WebGPU on Windows (141) and macOS (145); on Linux it is behind this pref
  launch.firefoxUserPrefs = { 'dom.webgpu.enabled': true, 'gfx.webgpu.ignore-blocklist': true };
}
if (process.env.PW_MOBILE) {
  const [width, height] = process.env.PW_MOBILE.split('x').map(Number);
  Object.assign(launch, { viewport: { width, height }, hasTouch: true, isMobile: browserName === 'chromium',
    deviceScaleFactor: 2.625, userAgent: 'Mozilla/5.0 (Linux; Android 14; Pixel 7) AppleWebKit/537.36 (KHTML, like Gecko) ' +
      'Chrome/154.0.0.0 Mobile Safari/537.36' });
}
const ctx = await pw[browserName].launchPersistentContext(resolve(process.env.PW_PROFILE ?? join(out, `profile-${browserName}`)), launch);
const finish = async code => {
  await ctx.close().catch(() => {});
  server.close();
  process.exit(code);
};
for (const s of ['SIGINT', 'SIGTERM']) process.on(s, () => finish(1));

// The emulated gamepad: navigator.getGamepads() returns it once 'pad' connected it (SDL's Emscripten joystick backend
// polls getGamepads every frame and learns of it from the gamepadconnected event).
await ctx.addInitScript(() => {
  const pad = { id: 'Party Board test pad (STANDARD GAMEPAD)', index: 0, connected: true, mapping: 'standard',
    timestamp: 0, axes: [0, 0, 0, 0], buttons: Array.from({ length: 17 }, () => ({ pressed: false, touched: false, value: 0 })),
    vibrationActuator: null, hapticActuators: [] };
  let connected = false;
  const real = navigator.getGamepads?.bind(navigator);
  navigator.getGamepads = () => (connected ? [pad, null, null, null] : (real ? real() : []));
  window.pbTestPad = {
    connect() {
      connected = true;
      const e = new Event('gamepadconnected');
      Object.defineProperty(e, 'gamepad', { value: pad });
      window.dispatchEvent(e);
    },
    button(n, down) {
      pad.buttons[n] = { pressed: down, touched: down, value: down ? 1 : 0 };
      pad.timestamp = performance.now();
    },
    axis(i, v) {
      pad.axes[i] = v;
      pad.timestamp = performance.now();
    },
  };
});

const page = ctx.pages()[0] ?? await ctx.newPage();
page.on('console', m => log(`${m.type()}: ${m.text()}`));
page.on('pageerror', e => log(`pageerror: ${e.stack ?? e}`));
page.on('crash', () => log('page crashed'));
let stopped = false;
page.on('console', m => { if (process.env.STOP_ON && m.text().includes(process.env.STOP_ON)) stopped = true; });
const cdp = browserName === 'chromium' ? await ctx.newCDPSession(page) : null;

log(`web_pw: ${browserName} ${ctx.browser()?.version() ?? ''}`);
await page.goto(`http://127.0.0.1:${webPort}/?${query}`);
const probe = await page.evaluate(async () => {
  const o = { ua: navigator.userAgent, gpu: !!navigator.gpu, opfs: !!navigator.storage?.getDirectory,
    createWritable: typeof FileSystemFileHandle !== 'undefined' && 'createWritable' in FileSystemFileHandle.prototype,
    syncAccess: typeof FileSystemFileHandle !== 'undefined' && 'createSyncAccessHandle' in FileSystemFileHandle.prototype,
    deviceMemory: navigator.deviceMemory, dpr: devicePixelRatio };
  if (navigator.gpu) {
    const a = await navigator.gpu.requestAdapter().catch(e => String(e));
    o.adapter = a && typeof a === 'object' ? { ...a.info, fallback: a.info?.isFallbackAdapter } : a;
  }
  try { o.storage = await navigator.storage.estimate(); } catch {}
  return o;
});
log(`web_pw: probe ${JSON.stringify(probe)}`);
if (disc !== '-') {
  await page.setInputFiles('#pick', resolve(disc));
  log(`web_pw: picked ${disc}`);
}

const touchAt = (type, x, y) => cdp.send('Input.dispatchTouchEvent', { type, touchPoints: type === 'touchEnd' ? [] : [{ x, y, id: 1 }] });
const steps = (process.env.PW_STEPS ?? '').split(';').filter(Boolean).map(s => {
  const [at, action, ...rest] = s.split(':');
  return { at: +at, action, args: rest };
});
const run = async ({ action, args }) => {
  const nums = (args[0] ?? '').split(',').map(Number);
  const ms = +(args[1] ?? 100);
  switch (action) {
    case 'key': await page.keyboard.press(args[0], { delay: 80 }); break;
    case 'hold': await page.keyboard.down(args[0]); await sleep(ms); await page.keyboard.up(args[0]); break;
    case 'tap': await page.touchscreen.tap(nums[0], nums[1]); break;
    case 'touch': await touchAt('touchStart', nums[0], nums[1]); await sleep(ms); await touchAt('touchEnd'); break;
    case 'drag': {
      const [x, y, x2, y2] = nums;
      await touchAt('touchStart', x, y);
      for (let i = 1; i <= 10; i++) { await sleep(ms / 10); await touchAt('touchMove', x + (x2 - x) * i / 10, y + (y2 - y) * i / 10); }
      await sleep(300);
      await touchAt('touchEnd');
      break;
    }
    case 'pad': await page.evaluate(() => window.pbTestPad.connect()); break;
    case 'padbtn':
      await page.evaluate(n => window.pbTestPad.button(n, true), nums[0]);
      await sleep(ms);
      await page.evaluate(n => window.pbTestPad.button(n, false), nums[0]);
      break;
    case 'padaxis':
      await page.evaluate(([i, v]) => window.pbTestPad.axis(i, v), nums);
      await sleep(ms);
      await page.evaluate(i => window.pbTestPad.axis(i, 0), nums[0]);
      break;
    case 'size': await page.setViewportSize({ width: nums[0], height: nums[1] }); break;
    case 'reload': await page.reload(); break;
    case 'click': await page.click(args.join(':')); break;
    case 'pick': await page.setInputFiles('#pick', resolve(args.join(':'))); break;
    case 'eval': log(`eval: ${JSON.stringify(await page.evaluate(args.join(':')).catch(e => String(e)))}`); break;
    case 'evalfile': log(`eval: ${JSON.stringify(await page.evaluate(readFileSync(args.join(':'), 'utf8')).catch(e => String(e)))}`); break;
    case 'dump': {
      // a file of the game's FS (e.g. a --hash-log) to OUT/<basename>
      const b64 = await page.evaluate(path => {
        const b = window.pbModule.FS.readFile(path);
        let s = '';
        for (let i = 0; i < b.length; i += 8192) s += String.fromCharCode(...b.subarray(i, i + 8192));
        return btoa(s);
      }, args.join(':'));
      writeFileSync(join(out, basename(args.join(':'))), Buffer.from(b64, 'base64'));
      break;
    }
    case 'shot': await page.screenshot({ path: join(out, `${args[0]}.png`) }).catch(e => log(`shot: ${e}`)); break;
    case 'mem': {
      const m = await page.evaluate(() => ({ js: performance.memory?.usedJSHeapSize, wasm: window.pbModule?.HEAP8?.length }));
      const p = cdp ? (await cdp.send('Performance.getMetrics').catch(() => ({ metrics: [] }))).metrics : [];
      log(`mem: ${JSON.stringify(m)} ${p.filter(x => /Heap/.test(x.name)).map(x => `${x.name}=${x.value}`).join(' ')}`);
      break;
    }
    default: log(`web_pw: unknown step ${action}`);
  }
  log(`web_pw: ${action} ${args.join(':')}`);
};
if (cdp) await cdp.send('Performance.enable');
const every = +(process.env.SHOT_EVERY ?? 10);
for (let s = 1; s <= +secs && !stopped; s++) {
  await sleep(1000);
  for (const step of steps.filter(st => st.at === s)) await run(step).catch(e => log(`web_pw: ${step.action} failed: ${e}`));
  if (s % every === 0) await page.screenshot({ path: join(out, `shot-${String(s).padStart(3, '0')}.png`) }).catch(e => log(`shot: ${e}`));
}
await page.screenshot({ path: join(out, 'shot-last.png') }).catch(e => log(`shot: ${e}`));
log(`web_pw: page errors: ${JSON.stringify(await page.evaluate(() => document.getElementById('error')?.textContent).catch(e => String(e)))}`);
await finish(0);
