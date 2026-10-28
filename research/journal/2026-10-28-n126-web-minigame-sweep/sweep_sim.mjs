import { readFileSync } from 'node:fs';
const src = readFileSync(process.argv[2], 'utf8');
const pad = src.slice(src.indexOf('const PAD = `') + 13, src.indexOf('})();`;') + 5).replace(/\\`/g, '`').replace(/\\\$/g, '$');
let frame = 0;
globalThis.window = { pbModule: { _pbnet_frame: () => frame } };
Object.defineProperty(globalThis, 'navigator', { value: {}, writable: true }); globalThis.setInterval = () => {}; globalThis.dispatchEvent = () => {}; globalThis.Event = class {};
eval(pad.replace('(() => {', '(() => { window.__sweep = () => minigameSweep();'));
window.__pbMinigame(40);
const rows = [];
for (frame = 1000; frame < 1000 + 2400 + 1500; frame++) {
  const s = window.__sweep();
  if (!s) continue;
  const t = frame - 1000 - 2400;
  if (t < 20 || (t >= 480 && t < 500) || (t >= 960 && t < 980) || t === 1020) rows.push(`${t} ${['a','b','x','y','l','r'].filter(k => s[k]).join('')} ${s.stick.map(v => Math.round(v * 127))}`);
}
console.log(rows.join('\n')); console.log(window.__pbStick);
