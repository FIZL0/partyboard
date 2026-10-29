// N140: evaluated in the page (tools/web_pw.mjs evalfile:, any browser, no WebGPU needed): reads the stored disc with
// pb.js's discBlockReader at fixed and random offsets (block boundaries, the end, reads spanning blocks, past the
// end) and compares each with File.slice().arrayBuffer() (async). Resolves to a summary string.
(async () => {
  const file = await storedDisc();
  if (!file) return 'no stored disc';
  const read = discBlockReader(file);
  const B = 1 << 20, n = file.size;
  const cases = [[0, 32], [B - 8, 16], [3 * B - 1, 2 * B + 2], [n - 16, 16], [n - 4, 64], [n + 10, 8], [0x440, 0x2000]];
  let seed = 140;
  const rnd = m => (seed = (seed * 1103515245 + 12345) >>> 0) % m;
  for (let i = 0; i < 200; i++) cases.push([rnd(n), 1 + rnd(i % 10 ? 70000 : 3 * B)]);
  let bad = 0, bytes = 0;
  const t0 = performance.now();
  for (const [at, len] of cases) {
    const dest = new Uint8Array(len + 8).fill(0xA5);
    const got = read(at, len, dest, 4);
    const want = new Uint8Array(await file.slice(at, at + len).arrayBuffer());
    let ok = got === want.length && dest[3] === 0xA5 && dest[4 + got] === 0xA5;
    for (let i = 0; ok && i < got; i++) ok = dest[4 + i] === want[i];
    if (!ok) { bad++; console.error(`readcheck: ${at}+${len}: got ${got}, want ${want.length}`); }
    bytes += got;
  }
  const head = new Uint8Array(8);
  read(0, 8, head, 0);
  return `readcheck: ${cases.length} reads, ${bad} bad, ${bytes} bytes, ${(performance.now() - t0).toFixed(0)} ms ` +
    `(with the async reads), ${file.name} ${n} bytes, head ${[...head].map(b => b.toString(16).padStart(2, '0')).join('')}`;
})()
