# Water and shadow shading upgrade — 2026-10-23

The DXR pass (`runtime/gs/src/ray_water.hlsl`) already traced reflections and native-light shadows,
but its shading was well below current games: four fixed cosines made the water a near-flat smeared
mirror, absorption was a single grey scalar (2.7% at 1000 units), and the shoreline was a +0.018
constant. Shadows used four fixed sample offsets (four discrete penumbra steps), point lights were
hard, and nothing occluded the ambient term.

## World scale and native light balance

`GSR_RAY_SCENE_DUMP` at the Surdana pond (`fixture-open-pond`): Rynn's resident mesh is 835 units
tall, so the shader uses `kMetre = 500`. The exported ambient is (0.42, 0.48, 0.60) against a sun of
(0.69, 0.64, 0.53) at about 28 degrees elevation, so a full sun shadow can only remove about 37% of the
light on flat ground. The shadow ratio method (lit / unshadowed applied to the GS image) is kept;
contrast comes from occluding the ambient term, not from changing the native lights.

## Changes

Shadows (`ray_shadows`):

- Directional light: four Vogel-disk samples over a 0.006 rad sun disk, rotated per pixel by
  interleaved gradient noise. The noise is keyed to jitter-compensated pixel coordinates (whole-pixel
  jitter shifts it with the image, the subpixel phase changes it per frame), so DLSS integrates it
  and a still, unjittered camera keeps one stable pattern.
- Point lights: a 6 cm emitter, two rotated samples (soft instead of hard edges).
- Normal-offset terminator bias (up to 10 cm at grazing light): removes the triangle-shaped
  self-shadow stripes on low-poly cliffs where the smooth GS lighting is still lit.
- Ray-traced ambient occlusion: four cosine-distributed rays, 1.5 m reach, linear falloff,
  alpha-tested and instance-opacity aware, applied to the native ambient term only. Open ground is
  bit-exact unchanged. Not applied on water receivers or reflection hits.

Water (`modern_water`):

- Eight directional waves (0.18–4.3 m, deep-water dispersion speeds, RMS slope 0.031) in world
  space. The camera's world x/z now reaches the shader in `Limits.zw` (was pre-scaled phases).
- Waves shorter than about four pixels (footprint stretched by the grazing angle) fade out and their
  slope variance becomes GGX roughness. This replaces distant aliasing/moiré with a widened sun path.
- Glossy reflections: four rotated samples over the roughness-dependent lobe; rays are kept above
  the water plane.
- Refraction offset scales with wave slope and thickness.
- Beer–Lambert absorption over the view path, strongest outside the level's own water hue, converging
  on the native water colour instead of a fixed blue.
- Caustics on the bed, driven by the visible sun share and faded by depth and pixel footprint.
- Backlit wave-crest scattering towards a low sun.
- Shoreline foam over the first 6 cm of vertical depth, broken into drifting noise patches and faded
  once unresolved (otherwise distant banks get a 1-pixel bright outline).

Menu tooltips and the `ray_shadows` INI description now name soft shadows and ambient occlusion.

## Tests

`gsr_test_ray_water` passes. Changed assertions, all keeping their regression intent:

- Shallow-reflection probe moved to row 50, 80 units along the ray (inside the old 96-unit band
  that the test guards against) but 40 units deep, past the foam line. The old pixel (1 cm deep) now
  correctly shows foam; a new assertion requires it to brighten.
- Point-light "blockers beyond the light" moved to z=500, outside the occlusion reach.
- Wave-motion continuity steps are 1 ms instead of 10 ms because the waves now move at physical
  speeds. Measured maximum jump 0.0015 against the 0.02 limit; nearest sampling jumps by about 1.
- New: at the 1 cm bank, at least a quarter of the row must brighten with foam (foam is patchy,
  so a single probe pixel can fall between patches).
- New: ambient occlusion darkens 846 pixels near a blocker with the sun behind the receiver, while
  open ground and an out-of-reach blocker leave every pixel unchanged.
- The `--benchmark` mode now reports D3D11 GPU timestamps around each frame (the D3D11 timeline
  waits for the D3D12 pass). On its synthetic scene the original and new shaders both measure
  0.77–1.01 ms median over three runs; that scene has too few shaded receivers to rank shader cost.

## Gameplay evidence

Matched effect-off/on frames at epoch 200 (`GSR_MATCHED_COMPARE`, 1920x1080, GS scale 3, DLSS off),
evidence under `build/water-shadow-upgrade-20261023/` (`before-*`, `after2-*`, `final-*`).

- Snowy river (`fixture-coast-bank`): the smooth smeared mirror becomes a rippled surface that keeps
  the cliff reflection broken up by waves; no horizon moiré after the grazing-footprint fix.
- Surdana pond shadows, world ROI ratio on/off (final build): 2nd percentile 0.796 → 0.729,
  0.5th percentile 0.744 → 0.638, pixels darkened by over 3%: 70,407 → 168,681. Soft contact occlusion is visible
  under the boots and target legs; the sun shadows remain subtle because of the native ambient share.
- Pond far bank: no bright outline in the final build (an intermediate build that averaged
  unresolved foam drew a 1-pixel rim); ripples keep the tree reflections.
- `fixture-cave-water` lands on a snowy hillside (no water in view); the flat snow shows no visible
  noise pattern.

The runtime ctest suite passed 41/43 in a run concurrent with the game build: `ray_water` failed on
the then single-pixel foam probe (fixed as above) and `spu2_test` failed under that load; both pass
when rerun.

Each run's private card copy has `Drakan 2.cfg` rewritten by the game, so the harness reports a
save mismatch; original and fixture saves are unchanged.

## Limits

No performance verdict: in-game FPS overlays of instrumented captures are not benchmarks, and the
earlier profile put the whole trace at about 0.08 ms. Ambient occlusion adds four short rays per
shadowed pixel and point lights trace two rays instead of one. Without DLSS, penumbrae and occlusion
keep a fine static dither (hidden by the grass texture and the 1536→1080 downsample in these
captures); there is no spatial denoiser. Water direction is a fixed wind, with no flow along
rivers. Foam ignores objects that only intersect the water outside the depth buffer.
