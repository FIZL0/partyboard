// node cdp_eval.mjs WS_URL EXPR: evaluate EXPR in the page, print the result
const ws = new WebSocket(process.argv[2]);
ws.onmessage = ev => { const m = JSON.parse(ev.data); if (m.id === 1) { console.log(JSON.stringify(m.result?.result?.value ?? m.result ?? m.error)); process.exit(0); } };
ws.onopen = () => ws.send(JSON.stringify({ id: 1, method: 'Runtime.evaluate', params: { expression: process.argv[3], returnByValue: true, awaitPromise: true } }));
