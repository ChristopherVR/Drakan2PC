# WebGPU renderer: the browser on the shared path (phase 5)

Plan: [webgpu-renderer.md](../../proposals/webgpu-renderer.md). Previous step: [webgpu-linux.md](webgpu-linux.md).

The browser build now runs the code Windows and Linux run: the GS bridge with the WebGPU renderer, the input module and
the settings overlay with the modern title / pause / save / Tab screens. The raylib WebGL window, the CPU GS presenter
and the raylib UI are no longer used by the browser build.

## Threads

A browser thread that owns WebGPU objects can never block: buffer maps, adapter and device requests resolve through its
event loop. The GS emulation reads back synchronously (local memory, aliased textures, captures). So:

- `rhi::Device::open` returns, in the browser, a proxy (`rhi_proxy.cpp`). Every call is recorded with its arguments
  copied into ordered batches (1024 commands / 8 MB, at most 16 in flight) for a dedicated GPU pthread, which owns the
  device and the canvas as an OffscreenCanvas (`-sOFFSCREENCANVAS_SUPPORT`, `emscripten_pthread_attr_settransferredcanvases`).
  Batches arrive through Emscripten's proxying queue, so the GPU thread returns to its event loop between them.
- Objects made through the proxy are placeholders whose real object the GPU thread creates in order; their destructor
  queues the release, so real objects always die on the GPU thread. Calls with a result (`readTexture`,
  `createProgram`, `configureSurface`) send the pending batch and wait: `readTexture` becomes `readTextureAsync` on the
  GPU thread and the GS thread sleeps until the map completes.
- Natively `DRAKAN_RHI_PROXY=1` runs the same proxy on a `std::thread`: the synthetic GIF suite passes 263 / 263 through
  it (about 28 % slower than direct), which is how the proxy is tested outside the browser.
- `drakan::gpu::Context::createAsync` / `readTextureAsync` are the asynchronous forms; the native blocking calls are
  built on them. Shader compile errors are not waited for in the browser (the uncaptured-error callback reports them).

## Browser-only findings

- Emscripten 4.0.23's emdawnwebgpu sends the dual-source blend factors as `"one-minus-src1alpha"`; the spec's name is
  `"one-minus-src1-alpha"` and Chrome rejects the pipeline. The browser device does not request dual-source blending, so
  the renderer uses its shader-blend path there.
- A canvas texture only lives until the current task returns, and a frame's commands can arrive in several tasks: the
  browser device draws the frame into a back buffer and copies it to the canvas texture inside `present()`.
- Chrome rejects a non-finite `depthClearValue` even with `loadOp: load` (wgpu-native accepts the undefined NaN).
- The HD pack is an index of empty placeholders in the browser file system; `texup::readImageFile` now streams the
  image from the launcher (`DRAKAN_TEXTURE_URL`, synchronous fetch on the calling worker), as the CPU path's pack did.

## Browser host pieces

- `drakan_window_web.cpp`: the canvas as the window (size from `drakan_web_resize`, focus, fullscreen through
  `player.js`, `keyDown` from the DOM key state).
- `drakan_web_events.h`: DOM input as lock-free single-producer rings, one per consumer, because the browser main thread
  (where `player.js` calls the exports) must never block. `drakan_input_web.cpp` is the input module's `HostInput`
  backend (drained on the host thread in `drakan_win::pump`), and the overlay reads its own ring through
  `OverlayInit::events` (`OverlayEvents.cpp`, ImGui fed from the event stream; F1 and the gamepad combo are read in
  `poll()` so they work while nothing is drawn).
- `player.js` sends the cursor position, wheel, typed characters and the first standard-mapping gamepad.
- `pre.js` selects the GPU renderer: with the canvas transferred there is no WebGL fallback, so the browser build needs
  WebGPU.

## Validation (Chrome, WebGPU on the RTX 5080, Playwright)

- The launcher page: Play -> logo / title (modern title menu in the game's art) -> Continue -> modern Load Game screen
  with save thumbnails -> Surdana gameplay with HD textures -> F1 pauses through the modern pause menu and opens PC
  Settings. No console errors.
- Windows runtime tests 75 / 75; the synthetic suite 263 / 263 direct and through the proxy.

## Not done

- Frame-rate measurement in the browser.
- Fullscreen from the settings menu is best effort (browsers grant it only inside a user gesture; Alt+Enter works).
