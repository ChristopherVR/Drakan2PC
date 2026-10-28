# Terrain crease lines from ray-traced AO — 2026-10-23

Screenshots taken minutes after 68c9033 (ray-traced AO) show thin dark lines along terrain triangle edges
and per-face lighter/darker bands on grass, dirt and mountain slopes (`ray_shadows=1`, DLSS quality, scale 4).

## Cause

`ambientOcclusion` in `runtime/gs/src/ray_water.hlsl` launches its rays from the flat face normal of the
resident (faceted) terrain. Its four Vogel samples have fixed elevations of about 69, 52, 38 and 21 degrees.
Across any concave crease steeper than about 21 degrees, the lowest ray from a receiver near the edge hits the
neighbouring face at a short distance. With linear falloff this counts as almost full occlusion, so a band
of reduced ambient forms along the edge. Its width is about `x = t*sin(theta-phi)/sin(theta)` for t below
the 750-unit reach, which is a few tens of centimetres for a 25 degree crease. The GS image is Gouraud-lit,
so the shading is smooth across that edge, and the result is a dark line or per-face band that follows the
mesh. Before this AO, the shadow ratio was exactly 1 wherever the sun was visible, whatever the normal.

## Fix

AO now ignores hits on surfaces whose ray-facing normal lies within 30 degrees of the receiver normal, and
counts them fully beyond 60 degrees (smoothstep). Walls, overhangs, props and characters still occlude. The
sun and point-light shadow paths are unchanged.

## Verification

- New `gsr_test_ray_water` case: a 25 degree crease must leave every pixel unchanged. It fails with the
  old shader (assertion at the new case) and passes with the fix. The existing AO case still darkens
  exactly 846 pixels.
- Not yet verified in game. The next incremental build should be checked at the screenshot spots with
  `ray_shadows` on and off.

## Not addressed

Sun self-shadowing on coarse terrain (flat-normal faces shadowed by a neighbour that rises above a low sun)
also gives per-face edges. It predates the AO change (see the `before-*` matched frames in
`build/water-shadow-upgrade-20261023/`) and is left alone here.
