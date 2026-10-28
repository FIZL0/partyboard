// node cdp_file.mjs PORT PATH OUT: copy a file out of the page's FS (512 KB chunks: bigger CDP replies never arrived)
import { writeFileSync, appendFileSync } from 'node:fs';
const [port, path, out] = process.argv.slice(2);
const list = await (await fetch(`http://127.0.0.1:${port}/json`)).json();
const page = list.find(t => t.type === 'page' && t.url.startsWith('http://127.0.0.1'));
const ws = new WebSocket(page.webSocketDebuggerUrl);
let id = 0; const pending = new Map();
ws.onmessage = ev => { const m = JSON.parse(ev.data); if (pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); } };
const ev = expr => new Promise(r => { const i = ++id; pending.set(i, r); ws.send(JSON.stringify({ id: i, method: 'Runtime.evaluate', params: { expression: expr, returnByValue: true } })); });
await new Promise(r => ws.onopen = r);
const size = (await ev(`pbModule.FS.stat(${JSON.stringify(path)}).size`)).result.result.value;
writeFileSync(out, '');
const CH = 512 << 10;
for (let off = 0; off < size; off += CH) {
  const r = await ev(`(()=>{const d=pbModule.FS.readFile(${JSON.stringify(path)});let s='';const e=Math.min(d.length,${off}+${CH});for(let i=${off};i<e;i++)s+=String.fromCharCode(d[i]);return s})()`);
  appendFileSync(out, Buffer.from(r.result.result.value, 'latin1'));
}
console.log(`${size} bytes -> ${out}`);
process.exit(0);
