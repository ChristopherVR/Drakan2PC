# Faceted characters and stitched terrain with ray-traced shadows — 2026-10-24

User report with `ray_shadows` on: textures look "cartoony", Rynn's face and other characters look very bad,
and terrain looks stitched together. The deployed build already had 5df8517 (terrain diagonal split, see
[terrain-shadow-lines.md](terrain-shadow-lines.md)).

## Cause

The shadow pass (`runtime/gs/src/ray_water.hlsl`, `main`) multiplies the finished GS image by a ratio
`lit / unshadowed` computed for each pixel's resident triangle, using that triangle's flat face normal. The GS image is
smoothly (per-vertex) lit, so anything that varies per face shows up as facets:

- **Characters shadowed themselves.** Sun/point-light rays and the four AO rays from a model receiver hit the same
  model. AO reach was 1.5 m (750 units), almost Rynn's full height (835 units), so rays from her face hit her nose,
  hair and shoulders. Each low-poly face got its own darkening: a cel-shaded look.
- **Terrain faces shadowed their neighbours under a low sun.** The AO crease filter (2026-10-23,
  `terrain-ao-creases.md`) did not apply to the sun rays. A sun ray grazing a shallow concave crease hits the
  rising neighbour face and shadows a band of the flat face, which ends at the triangle edges.
- **AO counted every contact at full strength.** That drew dark bands where walls, props and ground meet,
  like ink outlines.

## Fix

- `primarySurface` returns the receiver's TLAS instance index when its triangle belongs to a native model
  (material flag 128). `visibility` and `ambientOcclusion` skip candidates from that instance, so a model gets no
  shadow or AO from its own geometry. Shadows from other models and from the level still reach it.
- `creaseWeight` (shared with AO) now also weights sun/point-light occluders: hits facing within 30 degrees of the
  receiver are ignored, and those beyond 60 degrees count fully. The rule applies over the first 2 m of the ray and fades out by 5 m,
  so a distant gentle slope still shadows a valley at sunset.
- AO reach went from 1.5 m to 1 m, and its strength is capped at 60% of the ambient term (`kOcclusionStrength`).

## Verification

- New `gsr_test_ray_water` cases: a 10-degree sun across the 25-degree crease must leave every pixel unchanged.
  A model containing both a receiver plane and a blocker quad must not shadow itself. The same quad as a separate
  model instance must still shadow it (783 pixels). Both new cases fail with the previous shader, and all 44
  runtime tests pass.
- Live: matched shadows-off/on frames at epoch 200 at the Surdana pond (`fixture-open-pond`, 1920x1080, GS scale 3,
  DLSS off; `build/ray-shadow-facets-20261024/run.ps1`, a copy of the 2026-10-23 script). Rynn's on/off crops are
  practically identical. The 2026-10-23 build drew dark hatching over her shoulders, back and arms. In the on/off ratio
  map, Rynn, the target and the tree canopies no longer show their own outlines; the ground still gets Rynn's and the
  target's cast shadows. Pixels darkened by over 3%: 377,976 in the 2026-10-23 frames, 263,178 now.
- The overlay reads 42 fps against 69 fps in the 2026-10-23 capture. The scene now also draws tall grass that the
  earlier capture lacked, and capture overlays are not benchmarks; no performance conclusion.

## Follow-up: smooth shading normals

The per-face variation inside cast shadows remained after the self-shadow fix: a shadowed pixel darkens by
`ambient / (ambient + direct * N.L)`, and N.L used the flat face normal. Shadowed cliffs therefore showed one shade per
triangle in the on/off ratio map.

- Each ray triangle now has 7 float4 attributes (`kTriangleAttributes`, `runtime/gs/src/ray_normals.h`). The seventh
  holds its three vertex normals as 12+12-bit octahedral codes, stored as exact integer floats so no attribute
  can be a NaN bit pattern.
- Vertex normals are welded from exact shared positions (`weldCorners`/`smoothNormals`): area-weighted, limited to faces
  within 60 degrees of the corner's own face so hard edges stay sharp. The resident level is welded when its geometry
  changes. Model welds are kept per mesh `topology`, because every pose of an animated model is a new mesh with the same
  welds. Per-frame GS draws keep face normals, so their cache blocks stay independent.
- `shadingNormal` interpolates the vertex normals for direct-light N.L on receivers and reflected hits. Ray offsets,
  AO and the crease test still use the geometric normal.
- Tests: CPU checks that a ridge smooths and a cube corner stays sharp. A shadowed resident ridge must darken with no step
  larger than a quarter of its shading range (largest step 0.00024 of 0.006). That check fails with flat normals.
- Live: the cliff patches in the ratio map became smooth gradients. Ray-thread `rayModelAttributes` was about 1.75 ms
  per frame with per-pose welding; with topology-cached welds it is about 1.3 ms. The earlier 0.3 ms figure came from
  a different scene. The pass runs asynchronously, and `rayPack` stays near 2.3 ms.

## Follow-up: receivers on coarse terrain LOD

`primarySurface` matched a pixel to the resident grid along the view ray within 32 units or 2%. Coarse terrain LOD draws
the ground up to about 0.5 m above or below the grid. At grazing views that is far along the view ray, so distant
receivers faded out tile by tile. When the view-ray match is not certain, the receiver is now also searched straight
down and up (1 m, level geometry only, hits steeper than about 45 degrees ignored), and the more confident match wins.

- Test: ground drawn 150 units above or below a resident plane under a roof: at least 90% of the ground pixels
  must be shadowed (2024 and 1960 of 2048). Without the vertical search the test fails.
- Live: grass blades are not ray-traced geometry. They now take the shadow of the ground beneath them instead of staying
  bright on darkened ground beside the cliff. The overlay read 37.6 fps; the 8.9 fps in the smooth-normal capture was
  the instrumented readback, not the ray pass.

## Not addressed

- `synthetic` fails one check ("GUI page pack image on its first draw despite queued world upscales") while another
  session has uncommitted HD-texture work in `gs_texup`, `d3d11_textures.cpp` and `test_synthetic.cpp`. It is unrelated
  to these shaders. All other runtime tests pass.
