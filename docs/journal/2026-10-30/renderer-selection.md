# WebGPU renderer: one Windows binary with both renderers, and a portable temporal AA (phases 7 and 6)

Plan: [webgpu-renderer.md](../../proposals/webgpu-renderer.md). Previous step:
[webgpu-browser.md](../2026-10-29/webgpu-browser.md).

## Renderer selection (phase 7)

The plan was to delete the D3D11 renderer once WebGPU matched it. That would also delete DLSS, neural rendering and
DXR: NGX needs a raw Vulkan or D3D12 device, which `webgpu.h` does not expose, and wgpu-native's ray queries cannot be
enabled through its C API. So instead WebGPU becomes the default everywhere and the Windows game carries both:

- `gsr::BackendApi` (`gs_internal.h`) is the abstract renderer interface, its pure virtuals generated from the same
  method list as before. `gsr::Backend` owns one and forwards to it (`backend_select.inl`).
- `D3D11Backend` and `WgpuBackend` (`d3d11_backend_impl.h`, `wgpu_backend_impl.h`) implement it; each keeps its
  `Impl` as before. `createD3D11Backend` / `createWgpuBackend` are the factories.
- `backend_select_{d3d11,webgpu,both}.cpp` compile the forwarders for one or both. CMake: `gsr_core` (shared code),
  `gsr_d3d11_backend`, `gsr_webgpu_backend`, and `gsr_both`, which is `drakan::gs` on Windows. Linux and the browser
  link only WebGPU.
- `gsr::Config::api` (`Auto`, `D3D11`, `WebGpu`) picks at construction; `Auto` prefers WebGPU when it is built in. The
  GS bridge sets it from `[video] renderer` / `PS2X_GS`: `d3d11` selects Direct3D 11, anything else WebGPU (`sw`
  still selects the software GS before the bridge exists).
- The build script's `-Renderer` and the `GSR_API` CMake option are gone; `wgpu_native.dll` is always staged.
- `native_display_commit_test.cpp` drives the forwarders through a fake `BackendApi`
  (`GSR_BACKEND_FORWARDERS_ONLY`).

Play folders keep their `drakan.ini`, so one written by an older build still says `renderer = d3d11` and stays on
Direct3D 11 until changed.

## Temporal AA on WebGPU (phase 6)

`wgpu_temporal.cpp`. With the DLSS setting on any mode the WebGPU renderer runs a temporal AA over the 3D world:

- The world is detected as on D3D11 (a full-frame, depth-tested, non-sprite draw while the engine's scene camera is
  valid) and closed by the engine's EndWorld packet, so the HUD drawn afterwards is neither jittered nor accumulated.
- World draws get a Halton(2,3) sub-pixel jitter added in NDC.
- The resolve reprojects each pixel into the previous frame using the camera (`clip(current) * inverse(current) *
  previous`). The matrix math moved out of `nr_guides.cpp` into `guide_camera.cpp`, which both backends use. History
  is clamped to the 3x3 YCoCg neighbourhood of the current frame and blended at 0.1.
- Two history textures ping-pong, owned by the first full world target of a frame. History is dropped on a mode
  change, a size change, the engine's camera-cut flag, a gap of more than 12 frames, a missing depth buffer or a camera
  matrix that does not invert.
- Every mode renders at full internal resolution; there is no upscaling. The settings page says DLSS needs
  `renderer = d3d11`.

Fix found in game: `WgpuBackend::Impl::init` did not apply `Config::upscaleMode` (only later `setUpscale` calls did), so
a `dlss = quality` in `drakan.ini` left TAA off until the setting was changed in game. It now applies the start-up mode
and the `GSR_UPSCALE*` overrides as D3D11 does.

## Validation (Windows, RTX 5080)

- Runtime tests pass; synthetic GS suites 263/263 on D3D11, WebGPU and the threaded proxy.
- Game, `taa_tour.txt` in Surdana (still frame, mouse turn, settled): WebGPU on Vulkan logs "Temporal AA (portable)",
  with no ghosting while turning and the HUD sharp; the same run on `PS2X_GS=d3d11` starts DLSS SR as before. The thin
  outlines around the HUD orbs and the red streak in Rynn's hair appear on both renderers.

## Later the same day

Ray tracing and the modern water on WebGPU: [webgpu-ray-tracing.md](webgpu-ray-tracing.md).
