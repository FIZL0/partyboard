// node cdp_pause.mjs WS_URL [n]: pause the page n times (1 s apart), print the top of each stack
const ws = new WebSocket(process.argv[2]);
const n = +(process.argv[3] ?? 3);
let id = 0; const pending = new Map();
const send = (method, params = {}) => new Promise(r => { const i = ++id; pending.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
let paused = null;
ws.onmessage = ev => { const m = JSON.parse(ev.data); if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); } if (m.method === 'Debugger.paused') paused?.(m.params); };
ws.onopen = async () => {
  await send('Debugger.enable');
  for (let k = 0; k < n; k++) {
    const p = new Promise(r => paused = r);
    await send('Debugger.pause');
    const params = await Promise.race([p, new Promise(r => setTimeout(() => r(null), 5000))]);
    if (!params) { console.log('no pause'); continue; }
    console.log(`--- pause ${k}`);
    for (const f of params.callFrames.slice(0, 25)) console.log(`${f.functionName || '?'} ${f.url.split('/').pop()}:${f.location.lineNumber}:${f.location.columnNumber}`);
    await send('Debugger.resume');
    await new Promise(r => setTimeout(r, 1000));
  }
  process.exit(0);
};
