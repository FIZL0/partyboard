// N111 check: log navigator.vibrate calls, then touch the on-screen pad with synthetic TouchEvents.
(() => {
  const orig = navigator.vibrate ? navigator.vibrate.bind(navigator) : null;
  navigator.vibrate = function (p) { console.log('N111 vibrate ' + JSON.stringify(p)); return orig ? orig(p) : false; };
  let id = 1;
  const touch = (type, fx, fy, ident) => {
    const c = document.querySelector('canvas');
    if (!c) { console.log('N111 no canvas'); return; }
    const r = c.getBoundingClientRect();
    // SDL3's emscripten backend reads pointer events (pointerType "touch" = a finger)
    const ptype = { touchstart: 'pointerdown', touchmove: 'pointermove', touchend: 'pointerup' }[type];
    c.dispatchEvent(new PointerEvent(ptype, { pointerId: 10 + ident, pointerType: 'touch', isPrimary: true,
      buttons: type === 'touchend' ? 0 : 1, button: type === 'touchmove' ? -1 : 0,
      clientX: r.left + fx * r.width, clientY: r.top + fy * r.height, bubbles: true, cancelable: true }));
    console.log(`N111 ${type} ${ident} ${fx.toFixed(3)},${fy.toFixed(3)} canvas ${r.width}x${r.height}`);
  };
  const at = (s, f) => setTimeout(f, s * 1000);
  const T = +(new URLSearchParams(location.search).get('n111t') || 45);
  // first touch shows the pad (empty sky, no control)
  at(T, () => { touch('touchstart', 0.7, 0.3, id); touch('touchend', 0.7, 0.3, id++); });
  // START: top centre (N70 layout)
  at(T + 2, () => { const i = id++; touch('touchstart', 0.5, 0.085, i); at(0.2, () => touch('touchend', 0.5, 0.085, i)); });
  // stick: press its zone bottom-left, drag right then up
  at(T + 4, () => {
    const i = id++;
    touch('touchstart', 0.15, 0.8, i);
    at(0.3, () => touch('touchmove', 0.30, 0.8, i));
    at(0.6, () => touch('touchmove', 0.15, 0.55, i));
    at(0.9, () => touch('touchend', 0.15, 0.55, i));
  });
})();
