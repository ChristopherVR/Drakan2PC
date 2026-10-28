# Hard-edged light/dark regions on the pond surface with ray shadows on (23 October 2026)

Report: Rynn swimming on the Surdana pond (`build/verify-20261023/shore.txt`) shows large lighter and darker water
regions with straight edges. They appear with `ray_shadows=1` and are almost invisible with `GSR_RAY_SHADOWS=0`.

## Evidence

- Sun visibility is not the cause. A Python replay of a `GSR_RAY_SCENE_DUMP` (epoch 200) traced every water pixel's
  sun ray: the whole visible pond is behind terrain 2,000–8,000 units towards the low sun (materials 5/10, several
  hits per ray). `shadowRatio(waterLight)` is therefore the same everywhere (ambient / (ambient + sun·N·L)).
- `GSR_WATER_COVERAGE_DEBUG=3` (R = coverage opacity) shows the same polygons as the bug: opacity about 0.84 in some
  regions, 0.39 in others, 0 in one patch, with hard edges.
- `GSR_SURFACE_PICK` at epoch 97 (`shore0`) lists several native water-scope draws under one pixel: the surface
  (`tex0 7d80598207ec0`, vertex alpha 107–115), a dark pass (`7b805dc00bdc0`, rgb 0, alpha 13–66), a grey sheen
  drawn after EndWorld (`2007e96599307f35`, alpha 49–50, i.e. 0.38) and a faint pass (alpha 11).
- `waterSurface()` took the opacity from the **nearest** coverage triangle. CPU-clipped layers use different GS depth
  scales, so the nearest layer changes from triangle to triangle, and the opacity flips between about 0.85, 0.38
  and lower values.

The final blend is `lerp(shadowed GS colour, modern water, coverage)`. With shadows off the fallback is the original
GS water, which is close in brightness to the modern result, so the opacity regions hardly show. With shadows on,
the fallback is the GS water times the uniform shadow ratio (about 0.6), while the modern water keeps its unshadowed
sky reflection. Low-opacity regions therefore read as darker tiles.

## Fix

`ray_water.hlsl` `coverageOpacity()` traverses every coverage triangle along the view ray (non-opaque, no commits)
and uses the strongest layer's interpolated alpha. Coverage is proven as before by any hit. When only native grids
exist, the native triangle's alpha is still used.

`gsr_test_ray_water` adds a faint (0.38) overlay coverage layer nearer than the 0.85 surface layer and requires the
output to be unchanged. The old shader fails this assertion and the new shader passes.

Not yet verified in game: this needs an incremental game build.

## Follow-up: lighter hard-edged polygons in `shore2` (reflection path)

After the opacity fix, `shore2` (camera facing the cliff) still showed lighter polygons with straight edges. They
also appear with `GSR_RAY_SHADOWS=0` and disappear with `GSR_RAY_REFLECTIONS=0`
(`build/harness/s2-norefl/shore2.png`). Absorption and coverage are not the cause: `GSR_WATER_COVERAGE_DEBUG=3`
shows uniform opacity and saturated thickness over the polygons.

A scene dump taken with the `shore2` view held through epoch 200 (`build/shoredbg/shore2hold.txt`) was replayed in
Python. Each water pixel's mirror ray was classified by its first hit and by whether that hit is on screen with a
matching depth:

- The lighter region is where the reflected ray hits the castle wall above the cliff, off screen (native models
  10910496/10909920, materials 721063–721066). That hit uses `hitColor`'s material fallback. For native models the
  fallback was `toLinear(texel) * light.lit`: the native light was applied to the linearised texel. A GS vertex
  colour modulates the texel before display, and the terrain fallback already uses `toLinear(texel * vertexColour)`.
  In shadow (lit = ambient, about 0.5) the wall reflected about 2.3 times too brightly (`hold1`: +14 display levels
  against +0 for the terrain reflection).
- A dark diagonal strip inside it comes from nine untextured reconstructed triangles. They sit in the water-draw
  stream (between the coverage records), so they come from a water-scope pass that was not classified as water.
  They are tilted about 6 degrees through the water plane (y −178…−304 against the surface at −249), and reflection
  rays hit them 10–20 units after leaving the surface.

Fixes in `ray_water.hlsl`:

- `hitColor`: native-model hits are `toLinear(texel * vertex * light.lit)`.
- `waterLayer()`: reflection rays skip reconstructed (flag 256) triangles that lie within 15 degrees of the water
  plane and less than 0.25 m above the reflecting point.

New `gsr_test_ray_water` cases, each failing without its fix:

- An ambient-lit native model must reflect exactly like the same surface with that vertex colour.
- A black reconstructed layer tilted through the water must leave the image unchanged.

Remaining: the wall is a real reflector and is still reflected, about half as bright. Its silhouette still has
straight edges because the reflection is a near-mirror of a distant, flat-faced model.
