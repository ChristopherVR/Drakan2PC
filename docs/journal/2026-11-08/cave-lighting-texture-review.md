# Cave and arena lighting pops

User report: texture glitches, especially in multiplayer maps and caves, and the lighting changes frequently.

## Result

The lighting change is the ray pass's frame light set, not the native vertex lighting. It only happens with
ray-traced shadows on (`[video] ray_shadows` / `GSR_RAY_SHADOWS`).

The native shadow pass (`ray_water.cpp`, `wgpu_ray.cpp`) multiplies the GS colour by
`lit / unshadowed` computed from one light set for the whole frame. `drakan_scene_models.h` `Capture::capture`
built that set every frame from the camera:

1. the layer under the eye (closest ground below it, `>= -512`) gives the sun and ambient;
2. of that layer's point lights, the three with the highest `brightness * radius^2 / distance(eye)^2` are kept.

Each of these changes in one frame:

- **Torch re-ranking.** As the camera moves or only turns, a torch enters or leaves the top three. In a mine the
  layer's sun is shadowed by the ceiling, so without a torch the ratio is about `ambient / (ambient + sun)`
  (Grimstone Mines: ambient 0.07 to 0.14, sun 0.2/0.32/0.8). One torch entering the set lifts walls, beams and Rynn
  at once.
- **Layer switches.** Crossing into another layer swaps sun, ambient and all point lights at once.
- **No layer.** Over a terrain hole, below a layer or between layers nothing is selected: no lights and no ambient,
  so `hasContribution()` is false and the frame draws with no ray shadows and no ambient occlusion at all, then they
  return on the next frame with a layer.

### Evidence

- Replaying the selection on a RAM dump (`build/ootf9/h/runs/chain9/ram_grim_arrived.ram`, OotF Grimstone Mines;
  the selected layer has 10 point lights, 51 layers of 346 have more than 3): straight 12,000-unit camera walks gave
  15 light-set changes and 8 layer switches over 480 steps of 100 units; a 1500-unit camera orbit around four points
  changed the set 2 to 3 times per turn; 39 of 242 walk samples had no layer at all. The Grotto dump
  (`watchdog-stall-...ram`) sits over a hole: no layer for every sample, so no ray lighting there.
- In game, Crystal Mine (3B) start, a scripted 48-shot camera turn (`mouse 60 0` every 6 frames), ray shadows on:
  the pre-fix executable's mean world brightness jumps 35.6 to 45.9 in one shot when the brazier enters the set
  (walls, beams and Rynn all brighten). The same run with ray shadows off has no step above 2.4.

## Fix

`LightSelection` in `drakan_scene_models.h`, used by `Capture::capture`:

- a held light's score counts 1.5 times, so a newcomer must clearly outrank it;
- point lights fade in and out over 12 captures (about 0.2 s); a newcomer takes a slot only after the leaving light
  has faded, so the shaders still get at most the sun plus three point lights;
- sun and ambient move 1/8 of the way to a new layer's values each capture; a missing sun fades out;
- a capture with no layer keeps the last lighting for up to 90 captures (about 1.5 s, terrain holes and layer gaps);
  past that the former no-lighting result returns, so a cave with no layer under it is not lit by the outdoor sun;
- the first capture of a level is exact (no fade-in behind the load).

Same run after the fix: the brazier step becomes 35.6, 41.2, 43.6, 44.3, 45.8 over four shots, and the later
drop is spread too; every other shot matches the old run within 0.5. Largest per-shot change 10.3 to 5.6.
Fading is not removal: the set still changes, now smoothly and less often (Grimstone replay: 29 to 18 state changes
over the same walks and orbits).

Regression: `gsr_test_scene_models` covers the first-capture set, hysteresis, the fade-out-then-in swap with at most
three point lights, the hole hold and its expiry, sun/ambient blending and a fading sun.

## Not resolved

- Multiplayer arenas (Inquisitor's Lair depths) were not run. They are areas of the campaign levels and use the same
  capture, so the same pops apply.
- With ray shadows on, mines are darker than the native look while no torch is selected (Crystal Mine start:
  mean 35 against 43 with ray shadows off) because the layer's sun is shadowed by the cave ceiling. Physically
  consistent with the layer data, but a candidate for the "dark patches" part of the report.
- Texture glitches were not reproduced or bisected in this review; the HD pack, CLUT invalidation and mip paths were
  not examined. The lighting pops change whole surfaces at once and may be part of what was reported as texture
  glitches.
- One frame-wide set of three point lights is still an approximation of the game's per-object light sets; per-pixel
  light lists would need larger shader light arrays.
