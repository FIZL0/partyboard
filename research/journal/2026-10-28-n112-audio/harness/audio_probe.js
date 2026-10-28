// N112: logs, every 2 s, the AudioContext state, how many ScriptProcessor callbacks ran and the output's peak.
(() => {
  let calls = 0, peak = 0, ctx = null, last = performance.now();
  const orig = AudioContext.prototype.createScriptProcessor;
  AudioContext.prototype.createScriptProcessor = function (...a) {
    ctx = this; window.__pbAudioCtx = this;
    const node = orig.apply(this, a);
    let handler = null;
    Object.defineProperty(node, 'onaudioprocess', {
      get() { return handler; },
      set(h) {
        handler = h;
        node.addEventListener('audioprocess', e => {
          h(e);
          calls++;
          const d = e.outputBuffer.getChannelData(0);
          for (let i = 0; i < d.length; i++) { const v = Math.abs(d[i]); if (v > peak) peak = v; }
        });
      },
    });
    return node;
  };
  setInterval(() => {
    console.log(`AUDIO state=${ctx ? ctx.state : 'none'} calls=${calls} peak=${peak.toFixed(4)}`);
    calls = 0; peak = 0;
  }, 2000);
})();
