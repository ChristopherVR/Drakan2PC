# WebGPU GS renderer: core port (phase 2b)

Plan: [webgpu-renderer.md](../../proposals/webgpu-renderer.md).

## What exists

- `runtime/gs/src/wgpu_*.cpp` + `wgpu_backend_impl.h`: `Backend::Impl` on `gsr::rhi`. Target cache, depth, texture
  cache (decode, HD replacement, upscaling, palette HD), the draw policy (blend plan, HUD / menu / inventory maps,
  sprite footprints, grid capture), display composition, latch / present, captures, wide backdrops and HD movie
  frames keep the D3D11 code's logic; only GPU calls changed.
- `gs_shaders_wgsl.h`: WGSL port of `gs_shaders.h` / `gs_rt_sampling.inc` (uber shader with dual-source and shader
  blend variants, display, present), plus a uniform-placed rect program (WebGPU viewports must stay inside the target)
  and the native centre-sample gather used for compact readback and aliased textures at internal scales above 1.
- Build: `gsr_webgpu` builds on every WebGPU target (Windows, Linux). `GSR_API=webgpu` / `Build-LocalGame.ps1
  -Renderer webgpu` makes it `drakan::gs` on Windows and stages `wgpu_native.dll`; the default stays `d3d11`.

## Validation

- `gsr_test_synthetic_webgpu` (the synthetic GIF scenes and their 263 pixel checks): 263 / 263 on Windows Vulkan at
  internal scale 1, 2 and 3, Windows D3D12 and GL, Linux (WSL) llvmpipe Vulkan and GL.
- Saved scenes compared with the D3D11 renderer: 0 differing pixels at scale 1 (Vulkan, D3D12, GL) and 2 (Vulkan),
  and on Linux for the image both platforms save.
- Full game, Windows, `-Renderer webgpu`: Vulkan on the RTX 5080 at internal scale 4, HD movies, Surdana gameplay
  image matching the D3D11 run (`play_run.py` shots). No validation errors.
- The D3D11 `gsr_test_synthetic --scale 3` hangs in the shifted-alias coherence scene; the WebGPU build passes it.
  Not investigated (D3D11 is being retired).

## Not in the WebGPU build yet

- Settings overlay, HUD layers, modern title / pause / save menus and the FPS counter: done in phase 3
  ([webgpu-overlay.md](webgpu-overlay.md)).
- DLSS / DLAA, the temporal upscaler, neural guides, modern water and ray tracing (phase 6; the backend reports them
  unavailable).
- HD movie frames have no mip chain (D3D11 generated mips for minification below the HD size).
- Texture budget "auto" uses the 1.5 GB floor (no portable VRAM query).

## Findings

- wgpu-native aborts the process on an invalid submit: `wgpuTextureDestroy` on a texture still referenced by an
  unsubmitted encoder is fatal. `rhi` only releases textures (wgpu frees them after their last use).
- WGSL reserves `target`; the GL backend cannot sample one texture with two samplers, so point reads of render
  targets are `textureLoad` at the floored, clamped coordinate (bit-identical to D3D11 nearest sampling in the
  tests).
