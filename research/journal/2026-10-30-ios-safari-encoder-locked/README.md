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
