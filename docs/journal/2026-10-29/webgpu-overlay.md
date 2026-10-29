# WebGPU renderer: settings overlay, HUD and modern menus (phase 3)

Plan: [webgpu-renderer.md](../../proposals/webgpu-renderer.md). Previous step:
[webgpu-renderer-port.md](webgpu-renderer-port.md).

## What changed

- `settings/OverlayRenderer.h`: the graphics half of `SettingsOverlay` as an interface (ImGui draw data, textures,
  the back buffer copy the screen layer blurs and crops, the additive / opaque blend callbacks, readback).
  `SettingsOverlay` keeps the menu, screen layer, HUD layers and Win32 input; it no longer includes D3D11.
- `OverlayD3D11.cpp`: the previous D3D11 code (imgui_impl_dx11, blend states, frame copy) behind the interface.
  `OverlayInit::device / context` still create it, so existing callers are unchanged.
- `OverlayWebGpu.cpp` (`drakan_settings_webgpu`, every target with `drakan::gs_rhi`): ImGui drawn through the GS
  renderer's own `gsr::rhi::Device`, into the surface image, in the same command stream as the frame. Vertices are
  expanded from ImGui's index buffer (the rhi draws non-indexed float attributes). The "opaque" callback's constant
  blend factor is the fragment alpha (`SrcAlpha / OneMinusSrcAlpha`), so the rhi needs no blend-constant state.
  ImTextureIDs are `rhi::Texture*`, which lets the renderer's grid capture texture be drawn directly.
- `gsr::OverlayContext::api` says which handles the callback carries. The WebGPU present calls the overlay callback
  after the present pass with the rhi device, the surface image and the grid capture.
- The host glue (`drakan_settings_host.cpp`) picks the renderer from `api` and makes its save-preview textures and
  the `DRAKAN_OVERLAY_SHOT_DIR` readback through the overlay renderer, so it has no D3D11 code left.
- `drakan::gpu::Surface::copySource()`: the surface image is marked `CopySrc` only when the surface supports it; the
  frame copy and window screenshots are skipped otherwise (a copy from a non-`CopySrc` texture aborts wgpu-native).

## Findings

- MSVC's identical-code folding gives two empty functions the same address: callback "markers" compared by address
  cannot tell additive from opaque. The WebGPU callbacks are real functions acting on the renderer that is drawing
  (thread-local while `render` runs).
- ImGui skips shapes whose colour alpha is 0, even when a callback replaces the alpha later.

## Validation

- `overlay_webgpu_test` (runtime-tests): alpha blending, the additive and opaque (fade 0.25) callbacks, clip
  rectangles, an uploaded texture and the target copy, checked by pixel on Windows Vulkan, D3D12 and GL.
- Full game, `-Renderer webgpu`, Vulkan, 3840x2160 at internal scale 4 (`play_run.py` with a recipe and F1 posted to
  the window): the F1 overlay over the modern pause menu with the game art, the Tab screen (Inventory) over its
  blurred frame copy with the grid-captured item icons and the tutorial box, the FPS counter (about 150 fps in
  Surdana). No GPU validation errors.
