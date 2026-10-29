// N140: WEB_PRELOAD for tools/web_check.mjs: every 5 s logs the JS heap (performance.memory), the wasm memory's size
// and how many blob: XHRs (disc blocks) the page made.
(() => {
  let mem = null, blobs = 0;
  for (const name of ['instantiate', 'instantiateStreaming']) {
    const real = WebAssembly[name];
    WebAssembly[name] = async (...a) => {
      const r = await real.apply(WebAssembly, a);
      for (const v of Object.values((r.instance ?? r).exports ?? {})) if (v instanceof WebAssembly.Memory) mem = v;
      return r;
    };
  }
  const open = XMLHttpRequest.prototype.open;
  XMLHttpRequest.prototype.open = function (m, url, ...rest) {
    if (String(url).startsWith('blob:')) blobs++;
    return open.call(this, m, url, ...rest);
  };
  const t0 = performance.now();
  setInterval(() => {
    const m = performance.memory ?? {};
    console.log(`memlog t=${((performance.now() - t0) / 1000).toFixed(0)} jsUsed=${(m.usedJSHeapSize / 1048576) | 0}MB ` +
      `jsTotal=${(m.totalJSHeapSize / 1048576) | 0}MB wasm=${mem ? (mem.buffer.byteLength / 1048576) | 0 : '?'}MB blobXHR=${blobs}`);
  }, 5000);
})();
// time spent in blob: XHRs (synchronous: send() returns with the data)
(() => {
  const send = XMLHttpRequest.prototype.send, open = XMLHttpRequest.prototype.open;
  let ms = 0, n = 0;
  XMLHttpRequest.prototype.open = function (m, url, ...rest) { this.__blob = String(url).startsWith('blob:'); return open.call(this, m, url, ...rest); };
  XMLHttpRequest.prototype.send = function (...a) {
    const t = performance.now();
    try { return send.apply(this, a); } finally { if (this.__blob) { ms += performance.now() - t; n++; } }
  };
  setInterval(() => console.log(`memlog blobXHR n=${n} send=${ms.toFixed(0)}ms`), 5000);
})();
