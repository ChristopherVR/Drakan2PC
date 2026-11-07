# Issue 1: WebGPU ray tracing performance, artifact check on WebGPU, view 4 dashes, attack modes

Follow-up to [issue-1-rt-and-combat-status.md](issue-1-rt-and-combat-status.md). Rabe reported that the WebGPU
renderer with ray-traced reflections, global illumination and shadows runs notably worse than Direct3D 11.

Setup: private play folder `build/rt13/play` (never `dist`), Surdana start, internal scale 4 (render target 2048 x 2048),
VSync off, uncapped world rate, no upscaler or TAA, HD textures off, RTX 5080 and Ryzen 7 9800X3D. Throwaway runner
`build/rt13/perf_run.py`, captures under `build/harness/rt13-*`. Other agents ran games for most of the session, so
every comparison below was repeated in `rt13-q-*` once no other `drakan.exe` had run for 30 s (the numbers below).

## Measuring

`GSR_RAY_GPU_TIMING=1` only timed the DXR pass. The WebGPU device now requests `WGPUFeatureName_TimestampQuery` when
that variable is set (profiling only), `rhi::Device` gained `beginDispatchTiming` / `endDispatchTiming` (timestamp writes
on each compute pass, resolved and mapped asynchronously, never waited for), and `WgpuRay` logs one `[ray-gpu]` line
per pass: GPU milliseconds per dispatch (`sky`, `low`, `transport`, `main`), sizes, instances, frame triangles, nodes,
upload bytes and the CPU stages. `scripts/perf/ray_gpu_summary.py <stdout.log> [--budget transport=25]` prints the
medians (both backends' lines) and exits 1 when a stage's median is over its budget.

## Cause

Medians, all three effects on (`rt13-q-old` = the previous WebGPU configuration via `GSR_RAY_TRANSPORT_SCALE=2
GSR_RAY_DIFFUSE_RAYS=4`; it already includes the receiver change below, so the real old `main` was higher):

| Pass | WebGPU before | WebGPU after | D3D11 / DXR |
| --- | --- | --- | --- |
| shadows / water (`low`, 512 x 512) | 7.3 ms | 5.9 ms | in compose |
| GI + opaque reflections (`transport`) | 142.7 ms (1024 x 1024) | 16.5 ms (512 x 512) | 1.6 ms |
| composition (`main`) | 6.3 ms | 5.7 ms | 4.3 ms |
| total GPU | 156.8 ms | 30.3 ms | 6.7 ms (incl. 0.7 ms AS build) |
| world frames per second | 13 | 56 | 122 |

CPU work of the WebGPU pass is about 4 ms (BVH builds 3 ms, models 0.8 ms, uploads 0.5 ms; 11 MB uploaded per frame,
1218 instances, 3.7k frame triangles over a 169k-triangle resident world) and is not the problem. One effect at a time
after the change: shadows 7.2 ms, GI 25.6 ms, reflections 0.2 ms; with nothing on the game runs 211 world fps (D3D11 235).

1. **Global illumination** was traced like the DXR pass: one transport pixel per 2 x 2 render pixels (1024 x 1024 at
   scale 4) with four diffuse bounce rays, each bounce hit lit with shadow rays per native light. With software BVH
   traversal in compute that was 140 to 230 ms per frame (the DXR pass needs 1.6 to 5 ms for the same work).
2. **Reflections** alone still ran the transport dispatch and a full-resolution receiver ray per pixel (17 ms) although
   only glossy materials reflect since `02dcb8e9` and no native material is glossy.
3. The composition traced one receiver ray per render pixel (4.2 million) to find the material key and normal for the
   transport filter.

## Fix (`perf(game)` commit)

- Transport at one pixel per native pixel (`max(2, internal scale)` render pixels, `GSR_RAY_TRANSPORT_SCALE`) with two
  bounce rays (`GSR_RAY_DIFFUSE_RAYS`), filtered by the same 5 x 5 joint bilateral filter on the coarser grid. Bounce
  hit shadows stay (dropping them saved only about 18 %). The DXR pass is unchanged.
- No transport dispatch when GI is off and no static, frame or model material is glossy (`kRayMaterialGlossy`).
- The composition reuses the guide of its block's transport pixel when the render pixel lies on that sample's plane and
  traces only otherwise (silhouettes, other surfaces).
- Rejected: index-only traversal stacks (re-testing each popped node instead of storing its distance) to cut private
  memory; three interleaved A/B runs had the transport 30 to 45 % slower, so it was reverted.

Tests: `gsr_test_ray_trace` passes all earlier checks (exact transport, material boundary, odd sizes, furnace) and adds:
native-resolution transport with two rays carries the same red bounce (within 12 levels of the half-resolution one) and
traces `(W / 4) x (H / 4)` pixels; reflections with no glossy material leave the image untouched and dispatch no
transport. `scripts/tests/test_ray_gpu_summary.py` covers the parser and the budget check. A live budget run:
`python scripts/perf/ray_gpu_summary.py build/harness/rt13-q-new/stdout.log --budget transport=25 --budget total=40`.

Remaining gap: WebGPU is still about 4.5 times the DXR GPU time with all effects, because it traverses in software.
Direct3D 11 stays the better choice on RTX hardware; on WebGPU, GI is the expensive effect.

## Artifact fixes on WebGPU (`02dcb8e9`)

Same exe, both backends, Surdana start, one effect per run against an effects-off run (`build/rt13/compare.py`,
`rt13-v-{webgpu,d3d11}-{off,refl,gi,shadows}`):

| Effect minus off (start / still shot) | WebGPU | D3D11 |
| --- | --- | --- |
| Reflections, mean RGB | -0.01 / -0.01 | -0.01 / -0.01 |
| Shadows, mean RGB | (-2.3, -2.0, -0.8) | (-3.1, -2.6, -1.2) / (-2.3, -2.1, -0.8) |
| GI, purple / blue pixel share on the ground (absolute image) | 0.02 % / 0.59 % (off: 0.02 / 0.61) | 0.02 % / 0.61 % |

- Milky reflections: gone on WebGPU too; reflections change nothing on land (no native glossy material).
- Black shadows: the valley floor keeps its brightness, the average darkening is the D3D11 tree-shadow level (was -9 to
  -14 levels before the fix on D3D11).
- GI seam colours: no purple, pink or blue bands on the ground; the GI image's hue statistics equal the off image's.
  Difference images of the WebGPU GI run are dominated by misregistration (Rynn's idle pose and a sub-pixel camera
  offset differ between slow and fast runs), so the absolute hue share is the measure used.

## View 4 dashes and the dirt path

`GSR_WATER_COVERAGE_DEBUG=4` now works on WebGPU as well (`rt13-v4-wg`, D3D11 `rt13-v4-dx`). A temporary source view
(static resident / reconstructed frame / native model, not committed) located the dashes:

- The **coloured dashes** near the ground and the pink and blue lines on the birch trunks are **native model triangles**
  (flower and grass cards seen edge on, small flat leaf models, the trunk model's own texture rows). View 4 shows their
  bare texel times vertex colour. They are real ray-scene content, not terrain; GI already skips cutout and blended model
  cards as bounce sources, so they do not show in the GI output.
- The **thin dark dashes along terrain cell edges** are pixels where the receiver ray finds no triangle: hairline cracks
  between neighbouring cells in the resident terrain (both backends, so geometry, not WebGPU traversal). Those pixels get
  no transport and fall back to the unshadowed raster; with the filter that is invisible in the GI output. Closing them
  needs vertex welding / T-junction repair in `ResidentScene` (or an anchor-ray retry on both shaders); not done.
- **Dirt path**: terrain overlays are GS draws with ABE, `ALPHA = 0x44` and vertex alpha below 128 (`clamp=0`, REPEAT;
  logged with `GSR_DRAWLOG`). `reconstructDraw` observes a draw only when `opaqueEligible` and emits frame triangles only
  for draws that match no resident triangle, so an overlay over resident terrain is neither: the ray scene keeps the
  base texture. Supporting it needs a second (overlay) texture projection and per-vertex blend weight per resident
  triangle in the packed attributes and both shaders. Left open; it affects GI albedo on paths only slightly.
- Wrap versus GS CLAMP was checked as a dash cause: terrain draws use REPEAT, CLAMP draws are model cards; not the cause.

## Directional attack modes in game (issue 1 item d)

Throwaway driver `build/rt13/dirmode.py` (reuses `directional_attack_check.py`'s pose reader and `disc_combat_check.py`
with the private exe, `[keyboard] directional_attack` set, the `build/final-game/mc0` Surdana card copied; the source
card was unchanged). Exe SHA-256 `8da9eafaf471b7892361d5071a12afc153d1e6b91eddec7080301116668f1e0a`, D3D11, 30 fps world.

| Mode | Run | Result |
| --- | --- | --- |
| `tap` | `rt13-dir-tap3` | 16 of 16: a movement key pressed 4 world frames before the attack gives the arrow move in all four directions (`0x20684`, `0x2069c`, `0x2069d`, `0x2069e`); a key held 40 frames gives the ordinary moving / strafing attack (`0x2077c`, `0x20686`, `0x20687`) and Rynn keeps moving (1205 to 3436 units) |
| `tap` | `rt13-dir-tap2` | 15 of 16: down once gave the plain attack (key 4 frames ahead under heavy machine load: the tap age is host time, so it can pass 0.25 s) |
| `tap` | `rt13-dir-tap` | 10-frame lead (0.33 s at 30 fps) is outside the window: plain attacks, as designed |
| `always` | `rt13-dir-always` | 12 of 12: fresh and held movement both give the arrow move in every direction |
