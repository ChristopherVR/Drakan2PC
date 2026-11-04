# Diffuse bounce and opaque reflections

The requested extension adds single-bounce diffuse transport and rough opaque
reflections to both native ray backends. These are independent of modern water
and direct shadows. Settings expose `ray_gi` (`GSR_RAY_GI`) and broaden
`ray_reflections` (`GSR_RAY_REFLECTIONS`) to opaque surfaces as well as water.
Both default off for fresh settings; explicitly saved reflection choices remain
respected. Previously reflections defaulted true but were inert without modern
water, so keeping that default after decoupling would silently enable ray tracing.

## Transport and materials

Secondary rays sample textured resident geometry, including geometry outside
the raster image. Diffuse rays use cosine-weighted hemispherical sampling and
evaluate the hit's native lighting with visibility. Specular rays sample a GGX
lobe. Both respect alpha-tested holes and bounded transparent layers; neither
recursively calls the transport estimator. Diffuse misses add no guessed ambient
light. Specular sky radiance comes from the existing observed environment.

The native assets do not expose calibrated PBR albedo/roughness/metallic maps.
The fallback is a rough dielectric (roughness 0.75, normal-incidence reflectance
0.04). Triangle and native texture records can supply explicit roughness and
reflectance. Two UNORM8 values fit into the previously unused fourth component
of packed normal attribute 6; the material layout remains seven float4 values.
World and native material caches invalidate when these parameters change.
Cutout/blended primary surfaces do not receive a glossy coating.

The bounce supplements the original raster lighting. Native terrain material
colors may already contain authored lighting, so this is an approximate hybrid
renderer, not calibrated path tracing or a replacement for every original light.

## Initial validation and correction

GPU fixtures in both backends cover offscreen red bounce, blocking geometry,
opaque reflections with different roughness/reflectance, foliage exclusion,
finite output, alpha preservation and disabled behavior. Material packing tests
cover quantization, invalid inputs, warm-cache mutation and texture rebasing.

The first real Surdana same-frame capture (`build/harness/rt-transport-surdana-20261104/`)
revealed structured sampling noise and excessive rough-surface grazing attenuation.
The initial smooth-surface Fresnel term did not match the integrated rough GGX
reflection estimator. The correction uses the sampled GGX directional reflected
energy for diffuse attenuation, including samples whose incident radiance is black.
A short specular ray must also not treat a distant cliff beyond its reach as sky.

The initial full-resolution four-plus-four transport samples were too costly
and noisy. The follow-up moves transport to a separate half-resolution pass,
filters the linear transport delta with surface guides, and composes it before
DLSS/temporal resolve. Original water/shadow passes retain their own behavior.

`GSR_MATCHED_COMPARE_EFFECT=transport` captures the same world frame with both
new effects off/on while retaining water and direct shadows. It requires SR off,
like the existing water/shadow comparison modes. Its PNG suffix remains
`water-off/on` because it uses the existing comparison output path.

## Automated validation

The final filtered implementation passed the full Release CTest suite: 103
executed tests passed, six data-dependent tests skipped, zero failures
(`build/rt-transport-final-tests.log`). DXR and WebGPU ray tests executed on
RTX 5080 hardware; shader initialization errors now fail the WebGPU test instead
of being mistaken for unavailable hardware.

Additional constant-radiance tests passed after the full suite: high-reflectance
receivers retain the original brightness across roughness values when all
incident radiance equals the original image. WebGPU also exercises 63x61
transport buffers and restoration to 64x64, checking dimensions and alpha.
Both renderer suites passed (`build/rt-transport-furnace-tests.log`).

A WebGPU resource-layout issue surfaced during integration: the RHI previously
limited sampled textures to four, silently truncating the six textures needed
by the filtered transport. It now supports the eight reserved sampled bindings
and explicitly rejects requests beyond them. Existing samplers and storage
bindings retain their slots.

## Deployment and gameplay validation

The final build was deployed by the standard incremental builder after the
concurrent performance sweep/retests released the play folder. The deployed
`dist/DrakanPC/drakan.exe` and `build/final-game/bin/drakan.exe` hashes matched:
`4e10aa19432cd419dbdfa348c0460644dbc0449da0601e0ded25d4d05934da73`.
Build receipt: `build/rt-transport-deploy.log`.

All three final fresh-card runs exited normally and their images were inspected:

- `build/harness/rt-transport-filtered-surdana-20261104/`: same-frame transport
  off/on at epoch 400, SR off, scale 2. Filtering substantially reduces the
  initial structured noise while retaining changed indirect light/reflections
  across terrain and native objects.
- `build/harness/rt-transport-dlss-shadowmire-20261104/`: scale 4, DLSS Quality,
  water/shadows/GI/reflections on. The log confirms `evaluate=1`, actual RGBA16F
  SR evaluation at 1364x1364 to 2048x2048, without reported evaluation failure.
- `build/harness/rt-transport-filtered-shadowmire-20261104/`: same-frame transport
  off/on at epoch 350, SR off, scale 2. The cave/character receive changed
  illumination. Dark polygonal native-water patches are present in both images;
  this extension does not fix the pre-existing water coverage/shading limitation.

Comparison PNGs use `transport-water-off.png` and `transport-water-on.png`.
The off image is copied before the HUD; comparisons must exclude HUD/host overlays.
These are gameplay smoke and visual checks, not a whole-game performance benchmark.
The first full-resolution prototype's FPS and the final screenshots were captured
under different concurrent workloads, so they are not a controlled speedup metric.
