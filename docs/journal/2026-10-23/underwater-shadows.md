# Underwater: camera shells in the DXR scene shadow everything (23 October 2026)

Report: while swimming submerged (modern water, ray reflections and ray shadows on), the seabed shows large
hard-edged light/dark polygons and Rynn looks flat and faceted.

## How the PS2 draws underwater

Captured with a scripted dive from `build/water-global-review/fixture-coast` (hold C = crouch/dive and W):

- The world is drawn normally. A translucent teal **12-triangle box model** around the camera (half extents
  5120/4096/5120, texture native id 136336) is drawn after the world, before the HUD. That box is the whole
  underwater look: matched-compare "off" images (taken before the post-world draws) show a bright, untinted seabed
  and a fully coloured Rynn. Rynn's washed-out, low-contrast appearance is native and is the same with every
  effect off.
- No fog-colour change or framebuffer feedback is involved. The native projected blob shadow under Rynn is also
  native.

## Divergence

With `GSR_RAY_SHADOWS=0` the submerged image matches native. With shadows on, the patches appear. A scene dump at
world epoch 200 (`GSR_RAY_SCENE_DUMP`, Rynn submerged) replayed in Python: every seabed probe's sun ray hits the
camera box (object 12139568, 4–6k units up). Without it, the rays hit a 528-triangle radius-10240 sky sphere
(material 11), centred 3567 units from the render camera. `drakan_scene_models.h`'s `cameraBackdrop` normally
removes that sphere, but it requires the sphere to be centred on the native camera to 0.125 units and to fill a
complete spherical shell, so it misses both the trailing sphere and the box.

All receivers inside the shells are therefore "in shadow". The shadow ratio `(ambient·AO)/(ambient + sun·N·L)`
then uses the per-triangle face normal (`Materials[i+4].zw/[i+5].x`). Each face gets a different flat factor:
hard-edged patches on the low-poly seabed and faceting on Rynn.

## Fix

`ray_water.cpp` `cameraShell()` leaves model instances out of the DXR scene (and the scene dump) when all of these
hold:

- at most 1024 triangles;
- origin within 16384 units of the camera;
- the world AABB encloses the camera by 2048 units on every axis;
- every triangle plane is at least 2048 units from the camera.

The plane condition rejects rooms and huts that enclose the camera, because their floor or walls lie inside the
clearance. On the underwater dump, and on four older dumps (coast, pond, pond-native, water-edges moving), this
excludes exactly the box and the sky spheres. The 2026-10-23 pond dump has no candidate. After the exclusion, the
replayed seabed probes are lit, except where distant real geometry (terrain, a large cliff model) blocks the sun.

`gsr_test_ray_water` adds an off-centre 12-triangle box around the camera and asserts that the land-shadow result is
unchanged. The test fails without the fix.

Not yet verified in game: the fix needs a game build, and none was run for this change.
