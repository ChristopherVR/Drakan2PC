# Water tiles left native after the mid-world preview viewport — 2026-10-23

Symptom: with `modern_water=1`, some water tiles near the shore (often the one closest to the camera) kept the
original flat PS2 water look, with hard grid-aligned edges, next to correctly shaded modern water.

## Evidence

Fixture: `fixture-open-pond` with Rynn moved to the pond shore (382176, 163000, 522638), current deployed exe,
`dlss=quality`, scale 4. Runs under `build/water-tile-20261023/t/water-tile-shore{1..5}`.

- `GSR_WATER_COVERAGE_DEBUG=1`: almost every visible water pixel hits the resident water grid (red) but has no
  coverage triangle. Only one 2048-unit tile is covered (white).
- `GSR_SURFACE_PICK=333,410` (epoch 300): the near water draw (`tex0=7040598207820`, inside the `0x161a78` scope,
  `waterPhase=1`) gives `world=0,0,0` for all three vertices. In the same frame, earlier terrain draws reconstruct
  correctly, and solving the camera from those picks puts the water vertices on a flat plane (y about 159850,
  which matches `GSR_SURFACE_AUDIT`'s 159802 for the same material in another frame). The geometry is fine, so the
  reconstruction itself is failing: `RasterToWorld::configure` refused a camera without `Valid`.
- `DRAKAN_DLSS_LOG`: the world camera is republished (same position) right after that water draw in the same
  frame. Republishing happens only after a different packet has been published. `DRAKAN_CAMERA_RESET_DIAG`:
  about one `preview` packet (`0x1e97a0`, published with `World|Valid` cleared) per gameplay frame, and two world
  packets per frame.

## Cause and fix

`Backend::noteSceneCamera` stores the mid-world preview packet in `sceneCamera`. Until the next world viewport,
the surface classifier in `d3d11_draw.cpp` failed its `World` gate, and `RasterToWorld` could not configure. The
native water scope often falls in this window, so its draws produced no coverage triangles. Because the shader
requires coverage (`requireWaterCoverage`), those tiles showed the original water.

The backend now keeps the last `Valid|World` camera of the open phase (`waterWorldCamera`). Draws inside the
native water scope use that camera when `sceneCamera` is not a world camera. Other draws, the submerged path and
the shader are unchanged. Draws in the preview window outside the water scope still miss classification
(opaque/material ray geometry); that is not addressed here.
