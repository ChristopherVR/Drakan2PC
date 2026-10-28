# Thin shadow lines on terrain — 2026-10-24

With `ray_shadows` on, the dirt bank near the shore (`build/verify-20261023/shore`, `land.txt`) showed thin
straight dark lines (an X pattern and long diagonals). Large hard-edged darker wedges also appeared, from the
same cause. Both are absent with `GSR_RAY_SHADOWS=0`.

## Evidence

- `GSR_RAY_SCENE_DUMP` plus `GSR_TEMPORAL_DUMP_EPOCH=200` gave the frame, the prepared depth and the DXR scene.
  Python replays of `primarySurface` and the sun rays are in the session scratchpad.
- The line pixels are the only bank pixels whose receiver confidence is non-zero. Elsewhere the GS depth lies
  up to 250 units (0.5 m) behind the resident terrain, beyond the 32-unit / 2% search window. So the shadow
  (from far canyon terrain) applies only in bands where the two surfaces meet.
- The GS-drawn terrain vertices (reconstructed draw triangles) lie exactly on the grid heights. Inside a cell,
  however, they follow the other diagonal. One sample predicted the unknown corner height (-1393) exactly.
  Across four dumps (shore twice, open pond, coast bank), every full-detail terrain sample matched the
  opposite split for both values of quad bit 0. Over 150 samples matched it; none matched the exported split.
- The two triangulations agree only along cell edges and the shared diagonal ends. That produces the line and X
  pattern.
- With the corrected split applied to the dump, 98.5% of near land pixels get full confidence (previously
  about 24%).

## Fix

- `drakan_scene_geometry.h`: bit 0 cleared now splits along (x+1,z)-(x,z+1), bit 0 set along (x,z)-(x+1,z+1).
  `layerGround` (scene light layer selection) uses the same split. Mask bits 0x50/0xa0 stay with the first and
  second half of each branch. That mapping is unverified, but only about 3 cells per level carry a
  single-half mask.
- `ray_water.cpp` `cameraShell`: open rings anchored on the camera are excluded as occluders, like closed
  shells. Two 128-triangle sky barrels (radius about 5000, 900–9600 units above the camera, horizontally
  centred on it, present at the pond and the shore) sit in the sun path of every receiver near the camera.

## Expected change

The shore bank is in the shadow of the canyon wall (65 m away along the sun, also why the water there has no
sun glint), so the whole bank now darkens consistently instead of only along lines. Terrain beyond the
full-detail range still uses the receiver fade where LOD morphing moves the drawn surface.
