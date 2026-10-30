# iOS Safari: "encoder state is not valid" at boot (2026-10-30)

User report, Party Board v0.1.5 web page (fizl0.github.io/partyboard) in Safari on an iPhone: right after bootDll's
ObjectSetup, `[FATAL | aurora::gpu] WebGPU error 2: encoder state is not valid`. Adapter "apple", framebuffer
3840x2880 scale 3. Not reproducible here: no WebKit with WebGPU runs on Linux (N93).

## What the message means (WebKit source, Source/WebGPU/WebGPU)

- The string is only made in `CommandEncoder::beginRenderPass` (and its Swift twin), when `prepareTheEncoderState()`
  fails: the encoder is `Locked` (an `Ended` encoder would first report "encoder state is Ended"). The pass comes back
  invalid, carrying "encoder state is not valid" as its error, and it overwrites the encoder's last error.
- Aurora ends every pass it begins (gfx/common.cpp `render`, aurora.cpp's present passes, the resample pass), so the
  lock comes from WebKit: in WebKit through at least 2026-09 (`RenderPassEncoder::endPass`), a pass that is invalid
  (its own validation failed) calls `m_parentEncoder->makeInvalid(...)` and returns **without `lock(false)`**. WebKit
  main (late 2026-10) unlocks on every path ("every path from here unlocks the encoder"). So in shipped Safari, one pass
  that fails validation locks the encoder, the next pass gets "encoder state is not valid", and that is the error the
  page sees. The first error is lost.
- Candidate for the first error: WebKit 322549@main (2026-10-02, f13e4f2621) fixed a false error on draw() with an
  instance-stepped vertex buffer smaller than arrayStride, but aurora has no `VertexStepMode::Instance` buffers. Not that.

## What changed

Aurora patch 0017 (`cmake/patches/aurora/0017-web-find-failing-pass.patch`): in the browser the first uncaptured
error is logged and play goes on. From the next frame, each render pass is submitted in a command buffer of its own
under a validation error scope, and the game stops with `WebGPU error N in '<pass label>': <message>`. If no render
pass is the one, it stops after a few more errors with "outside the render passes". Checked in headless Chromium with a
`WEB_PRELOAD` that breaks every "Render pass" after 20 s (`setViewport(-1e9, …)` before `end()`): the crash box reads
`WebGPU error 2 in 'Render pass 0': Viewport offset … is less than the minimum`.

Next: the user's crash box from the v0.1.7 page in iOS Safari names the pass and the real message.

## The cause (v0.1.7 page on the iPhone, same day)

The per-pass encoder named `'Render pass 0'` but no message: `GPUCommandEncoder.finish: encoder state is 'Locked'`
(the same WebKit lock, now inside the pass's own encoder, so finish reports the lock, not the pass's reason). WebKit's
`RenderPassEncoder::setViewport` (through 2026-09) calls `makeInvalid()` with no message when the viewport isn't inside
the render target (`x < 0`, `y < 0`, `x + width > target`, `y + height > target`); Chrome follows the current spec,
which allows it. A Chromium preload that logs every viewport breaking WebKit's rule found it from the first frame:
`'Render pass 0' target 1280x960 viewport 0,0 1280x962` (GX's 640x481, so 2886 rows of 2880 on the iPhone at scale 3).

Fix: aurora patch 0011 (the viewport code) now keeps the viewport inside the pass's target in the browser build (Safari
too); desktop is unchanged. With it the same preload logs none from boot to the file select, nor in a scripted party
through a board to a minigame's instructions (`safari_vp.js` below, a `WEB_PRELOAD` for tools/web_check.mjs).

```js
(() => {
  const cv = GPUTexture.prototype.createView;
  GPUTexture.prototype.createView = function (d) { const v = cv.call(this, d); v.__w = this.width; v.__h = this.height; return v; };
  const begin = GPUCommandEncoder.prototype.beginRenderPass;
  GPUCommandEncoder.prototype.beginRenderPass = function (d) {
    const pass = begin.call(this, d), a = d.colorAttachments && d.colorAttachments[0];
    pass.__w = a ? a.view.__w : 0; pass.__h = a ? a.view.__h : 0; pass.__label = d.label;
    return pass;
  };
  const sv = GPURenderPassEncoder.prototype.setViewport;
  GPURenderPassEncoder.prototype.setViewport = function (x, y, w, h, mn, mx) {
    if (x < 0 || y < 0 || w < 0 || h < 0 || x + w > Math.ceil(this.__w) || y + h > Math.ceil(this.__h) || mn < 0 || mx > 1 || mn > mx)
      console.log(`SAFARI_VP '${this.__label}' target ${this.__w}x${this.__h} viewport ${x},${y} ${w}x${h}`);
    return sv.call(this, x, y, w, h, mn, mx);
  };
})();
```
