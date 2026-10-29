# One WebGPU renderer for Windows, Linux and browser

Decision (2026-10-29): replace the Direct3D 11 renderer (Windows) and the CPU GS / experimental WGSL raster
(Linux, browser) with one renderer written once against the standard `webgpu.h` API with WGSL shaders.

| Target | Implementation of `webgpu.h` | Native API underneath |
| --- | --- | --- |
| Windows x64 | wgpu-native v29.0.1.1 (pinned) | Vulkan, D3D12 fallback |
| Linux x64 | wgpu-native v29.0.1.1 (pinned, already used by the WGSL raster) | Vulkan, GL fallback |
| Browser | Emscripten 4.0.23 `--use-port=emdawnwebgpu` | the browser's WebGPU |

Code that is not tied to one API must not be duplicated per platform. Platform code is limited to creating a surface
from a window handle (HWND, X11 / Wayland, canvas) and to the browser's threading.

## Why this shape

`gs_core.cpp` (GIF / GS register emulation) is already platform free and drives `gsr::Backend`, whose only
implementation is `Backend::Impl` in `d3d11_*.cpp`: target cache, texture cache (decode, HD replacement, upscaling),
the GS pixel-pipeline uber shader (`gs_shaders.h`), display circuits / presentation, and the Drakan-specific parts
(widescreen backdrops, HUD scaling, inventory grid capture, scene camera / temporal data). The port therefore keeps
`gs_core`, `gsr::Backend` and `gsr.h` and moves `Backend::Impl` from D3D11 calls to `webgpu.h` calls. During the
port a build switch selects the implementation; D3D11 is deleted once the WebGPU build reaches parity on Windows.

## Phases

1. **Foundation** (`runtime/gpu`, `drakan::gpu`): pinned SDKs for all three targets, instance / adapter / device
   creation with backend preference (`DRAKAN_GPU_BACKEND=vulkan|dx12|gl`), error and device-loss reporting,
   surfaces from native window handles, synchronous waits and texture readback, a headless test.
2. **Core renderer port**: `Backend::Impl` on `webgpu.h`: targets, depth, texture cache, draw (uber shader in WGSL),
   readback for `flushToMemory`, display / present, capture, movie frames. Validated against D3D11 captures on Windows.
3. **Overlay on WebGPU**: the settings overlay, HUD layers and modern menus drawn through the renderer's own device
   (an `OverlayRenderer` on `gsr::rhi`; replaces the D3D11 overlay and, with phase 4, the raylib GL UI).
4. **Linux**: the same renderer and overlay; a GLFW (no client API) window replaces raylib's GL window.
5. **Browser**: the renderer runs on a dedicated pthread that owns the device and its event loop; GS calls are
   proxied to it, readbacks wait on proxy completion. The canvas is an OffscreenCanvas transferred to that thread.
6. **Enhancements on the shared renderer**: internal resolution, widescreen / HUD, texture filtering / upscaling,
   modern water, ray-traced shadows / reflections traced in WGSL compute over a BVH built on the GPU (the same
   code in the browser), a portable temporal AA / upscaler in place of DLSS.
7. **Remove** the D3D11 renderer and make WebGPU the default everywhere; the CPU GS stays as the fallback when no
   WebGPU adapter exists.

## Known costs

- DLSS: NVIDIA NGX needs a raw Vulkan / D3D12 device, which the `webgpu.h` C API does not expose. It is not
  available on the shared renderer until a Vulkan interop path exists; the settings page states this.
- Ray tracing: wgpu-native v29 lists `WGPUNativeFeature_RayQuery`, but it is experimental and its C API cannot
  enable experimental features (device creation fails with "experimental features are not enabled"). Compute
  tracing is slower than hardware ray queries but is one implementation for every target.
- The browser renderer needs cross-origin isolation and WebGPU (current desktop Chrome / Edge).

## Status

- 2026-10-29, phase 1 done: `runtime/gpu` (`drakan::gpu`) and `cmake/WebGpu.cmake`; `gpu_test` passes on
  Vulkan and D3D12 (Windows, RTX 5080) and GL (WSL).
- 2026-10-29, phase 2a done: `gsr::rhi` (`runtime/gs/src/rhi.h`, `rhi_webgpu.cpp`), the immediate-mode interface
  the `d3d11_*.cpp` code maps onto (about 40 distinct D3D11 calls in the core). `gsr_test_rhi` checks upload ordering
  against recorded draws, depth, blending, write masks, scissor, copies, oversized uploads, depth readback and
  dual-source blending on Windows Vulkan / D3D12 / GL and Linux GL / Vulkan. Findings: wgpu-native aborts the
  process on an invalid submit; depth readback is drawn into RGBA8 because GL cannot copy depth to buffers, render
  R32F, or `textureLoad` a `texture_depth_2d`.
- 2026-10-29, phase 2b done: `gsr_webgpu` (`runtime/gs/src/wgpu_*.cpp`, `gs_shaders_wgsl.h`) is `Backend::Impl` on
  `gsr::rhi` with the D3D11 backend's target / texture / draw / display policy. `GSR_API=webgpu` (build script
  `-Renderer webgpu`) makes it `drakan::gs` on Windows; D3D11 stays the default. Details and findings:
  [webgpu-renderer-port.md](../journal/2026-10-29/webgpu-renderer-port.md).
- 2026-10-29, phase 3 done: `SettingsOverlay` draws through `settings/OverlayRenderer.h`, with a D3D11
  implementation and a WebGPU one on the GS renderer's `gsr::rhi` device (`drakan_settings_webgpu`). The WebGPU game
  build has the F1 overlay, HUD layers (FPS counter, subtitles, crosshair, menu cursor) and the modern title / pause /
  save / Tab screens. Details: [webgpu-overlay.md](../journal/2026-10-29/webgpu-overlay.md).
- 2026-10-29, phase 4 done: native Linux runs the Windows path (GS bridge, WebGPU renderer, input module, settings
  overlay and modern screens) on a GLFW window without a client API; only the window (`drakan_window_glfw.cpp`), the
  input host (`drakan_input_glfw.cpp`) and the overlay's window platform (`OverlayGlfw.cpp`) are Linux-specific.
  Details: [webgpu-linux.md](../journal/2026-10-29/webgpu-linux.md).
- 2026-10-29, phase 5 done: the browser runs the same path. `rhi::Device::open` returns a proxy there whose device
  lives on a GPU pthread owning the canvas (OffscreenCanvas); calls are batched to it and readbacks wait on its event
  loop. Validated in Chrome to gameplay with the overlay. Details: [webgpu-browser.md](../journal/2026-10-29/webgpu-browser.md).
- 2026-10-30, phase 7 done, changed: both renderers are built into the Windows game behind `gsr::BackendApi`
  (`backend_select.inl`); `[video] renderer` picks one at start (`webgpu`, the default on every platform, or
  `d3d11`). Direct3D 11 stays as the optional Windows renderer for DLSS, neural rendering and DXR rather than being
  removed (no Vulkan / D3D12 interop for NGX through `webgpu.h`, see "Known costs"). The build script's `-Renderer`
  switch and `GSR_API` are gone. Details: [renderer-selection.md](../journal/2026-10-30/renderer-selection.md).
- 2026-10-30, phase 6 done: a portable temporal AA on WebGPU (`wgpu_temporal.cpp`; Halton jitter, camera
  reprojection through `guide_camera.cpp`, shared with the D3D11 guides, and a YCoCg neighbourhood clamp), turned on
  by the DLSS setting; modern water, reflections, ray-traced shadows and ambient occlusion traced in WGSL compute over
  CPU-built BVHs (`wgpu_ray.cpp`, `ray_trace.wgsl`, `ray_bvh.cpp`; the scene packing is shared with DXR in
  `ray_scene.cpp`); PC Settings > Display > Renderer switches WebGPU / Direct3D 11 on Windows. Details:
  [webgpu-ray-tracing.md](../journal/2026-10-30/webgpu-ray-tracing.md).
