# Issue 1: milky reflections, GI seam colours, black ray shadows

Reporter (RabeOhneNamen, latest comment on https://github.com/ChristopherVR/Drakan2PC/issues/1) tested each ray
effect alone: reflections wash some surfaces out, global illumination puts odd colours along level seams, ray
shadows turn some surfaces dark. Duplicate shadows and actor coverage were confirmed fixed. Their screenshots are
at the Surdana start (birch trunks, grass).

## Reproduction

Private copies of the play folder (`build/issue1-rt-play-base` = pre-fix, `build/issue1-rt-play-after`), Surdana start,
D3D11, one effect per run, modern water off, HD textures off (scratch wrapper around `shadow_review.py`). Captures:
`build/harness/issue1-rt-before/<case>/` and `build/harness/issue1-rt-after/<case>/`, difference images beside them.

## Reflections: rough reflection added to baked lighting

Before: `refl - off` averages +3.9 blue levels over the start view and lifts all ground and slopes towards the sky
colour (`diff-refl-start.png`). Every native material packs the default rough dielectric (roughness 0.75, F0 0.04),
so every opaque pixel traced a rough GGX lobe and replaced up to its Fresnel energy with sky and terrain radiance.
The native texture already holds its lit appearance; the rough lobe is close to the irradiance that lighting stands
for, so it was counted twice. Fix: `rayMaterialGlossy` / `kRayMaterialGlossy` (roughness at most 0.5) on the CPU, and
both shaders reflect only glossy, nonblended, non-cutout opaque materials. No native asset has authored roughness yet,
so the reflection setting now changes water only. That is the safe degradation; glossy materials (an HD pack or a
mod with authored roughness) still reflect, and the GGX furnace tests still run at roughness 0.04 and 0.5.

## Global illumination: foliage cards as bounce sources (and per-cell terrain materials)

Before: `gi - off` shows red and blue/purple bands on the ground near the grass strips and a purple lower half on a
birch trunk (`diff12-gi-start.png`; purple pixel share 1.0 % of the view). Diffuse misses add nothing, so GI only
shows where bounce rays hit nearby geometry. Three new DXR views (`GSR_WATER_COVERAGE_DEBUG`, see `env-flags.md`)
located it. `5` (radiance each visible surface returns to a bounce ray) shows nothing purple. `6` (incoming bounce
light alone) shows saturated pink, blue and green bands exactly where `4` (receiver colour) shows rows of pink and
blue flower and grass cards that the raster does not show at that size. The bounce rays hit the native cutout
foliage models (grass, flowers) near the ground; their ray-scene cards carry the bare texture colour without the GS
vertex tint and without distance fade.

Fix: diffuse bounce rays pass cutout or blended native model materials (`foliage()` in `ray_water.hlsl`, `kBounce`
in `ray_trace.wgsl`). Shadows, AO and reflections still see them. After: the bands and the trunk tint are gone
(`build/harness/issue1-rt-after2/diff12-gi-start.png`, purple share 0.6 %, all of it animation misregistration at
moving edges), mean added light (0.14, 0.14, 0.0).

Found on the way: a terrain layer stores one texture reference per cell (`level-format.md` section 10), but
`ResidentScene` kept one projection per layer (the largest drawn triangle's texture and UV plane for the whole
layer). `ResidentScene` now keeps the observed projection of each drawn cell; the layer projection only fills cells
never drawn. Taken alone this did not remove the bands (`build/harness/issue1-rt-after`), but it gives hit and
receiver colours the right cell texture. Still open: view `4` shows thin coloured dashes along some terrain cell
edges and the dirt path (a blended overlay pass) is absent from the ray scene; neither shows in the GI output now.

## Shadows: the whole valley in the cliffs' shadow

Before: `shadows - off` darkens the ground by 9 to 14 levels on average; the `direct` diagnostic alone does the same,
AO alone changes nothing (-0.5). The ratio image is flat over the whole floor: a solid occluder, not foliage. CPU
traces against the epoch-1000 DXR dump (`build/harness/issue1-rt-before/shadows/ray-scene.bin`, sun
(-0.67, 0.47, -0.57), 28 degrees): every ground point in a 12000 x 12000 unit grid around Rynn is blocked by static
terrain 18000 to 65000 units away, and most also by a 482-triangle rock model (`b037d`) 11000 to 14000 units away.
The opposite sun direction is blocked too: the start valley is enclosed by cliffs. The native renderer applies this
light without occlusion, and the level's look (a sunlit valley floor) depends on that. Fix: directional shadow
occluders fade out between 8 m and 16 m along the ray (`kSunShadowFade` / `kSunShadowReach`, both backends, primary
and secondary visibility). Characters, props, trees and nearby walls keep their shadows; point lights unchanged; the
native-owned character mask and the model export are untouched (no duplicate shadow, actors still covered).

## Tests

- `gsr_test_scene_geometry`: per-cell projections, layer fallback, no change on an equal observation, incremental
  equals fresh projection, glossy classification and packing.
- `gsr_test_ray_water` (DXR): near / 12 m / 18 m sun blocker (0.040 / 0.120 / 0.200 of a 0.2 input), default native
  material unchanged by reflections.
- `gsr_test_ray_trace` (WebGPU): a huge cliff 11000 units away leaves the ground exactly as lit as without it
  (fails with the reach set to 1600 m).
- `gsr_test_surface_builder`, `gsr_test_scene_models` still pass.
- No GPU fixture for the foliage bounce exclusion (it needs native model meshes in the transport scene); verified
  in game only.

## Live captures

Pre-fix `build/harness/issue1-rt-before/{off,refl,gi,shadows}` (+ `issue1-rt-before-direct`, `-ao` diagnostics);
post-fix `build/harness/issue1-rt-after/{off,refl,gi,shadows,all}` (reflection and shadow fixes, per-cell terrain)
and `build/harness/issue1-rt-after2/{off,gi,all}` (final). Reflections: `refl - off` mean (0.0, 0.0, 0.0), was +3.9
blue. Shadows: the floor keeps its native brightness and the trees, Rynn (native projector, one shadow) and props cast;
`shadows - off` -2.3 levels on average (tree shadows), was -9 to -14 over the whole floor. Only Surdana start was
captured, D3D11 only; WebGPU is covered by `gsr_test_ray_trace` alone.

