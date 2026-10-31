# Living foliage (experimental): wind for grass and trees

Request: make grass and trees look much better by today's standards, with grass and trees moving in the wind, as an
experimental setting. Option: `[video] foliage_wind` (+ `foliage_wind_strength`), F1 > Display > Vegetation.

## Why a CPU-side shear and not a vertex shader

The GS renderer only sees screen-space vertices (the VU1 microprogram has already projected everything), so a vertex
shader has no world position or model height to displace. The engine's own per-object render method does have both:
`0x189b38` (`this` = handle) reads the transform at `handle+0x14` / `+0x20` for every draw path. The hook wraps it, and for
vegetation shears the rotation's up column for the duration of the call (world-model.md section 12). It costs one resource
lookup per drawn object, works for D3D11, WebGPU and the software GS alike, and needs no shader or renderer change.

## What it does

* Wind: a slowly veering direction, long gust fronts travelling with the wind (about 23 cells apart) plus a faster second
  wave; plants lean 5 to 17 % of their height plus a 1.4 to 1.9 Hz sway and a 3.3 Hz flutter, trees 1 to 3 % with a slow
  0.38 Hz sway. Neighbours share most of their phase so the meadow shows waves rolling through, not noise.
* Interaction: plants within 4800 units of the controlled actor (Rynn, or Arokh) lean away from it (up to 0.55).
* The lean preserves the blade length (the top drops by lean^2 / 2) and is capped at 0.8.
* Strength 0.25 .. 2 scales all of it.

## Checks

* `world_foliage_test` (determinism, bounds, strength scaling, gusts travel, neighbours in step, push, shear length).
* `settings_test` / `settings_schema_test` (ini round trip, schema rows).

## Not done / next

* Ray-traced shadows of trees do not move (scene export keeps the true transforms).
* Linear shear moves the whole trunk; a per-vertex bend (canopy only) would need the model vertices, which the GS
  front end does not see. A leaf-flutter pass in the GS shader for alpha-tested foliage draws is the next step.
* The ids table covers the vegetation names found in the 14 level object lists; new levels need `foliage_ids.py`.
