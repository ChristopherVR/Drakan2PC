# Animated characters in the level viewport (2026-11-01)

Placed creatures and people now play a clip in the 3D view (idle by default) instead of standing in bind pose.

## Design

- `scene/animlayer.ts` `AnimationLayer` (owned by `ModelLayer.anim`). The instanced batches still draw every placed model
  in bind pose. For the nearest on-screen instances of classes that can animate, the layer hides that instance in its
  batch (zero-scale matrix, `ModelLayer.hideInstance`) and shows a CPU-skinned `SkinnedModel` playing a clip with root
  motion stripped (`inPlace`).
- Which classes: those with an animation field (type 9) in the schema (`editor/animsource.ts`, `hasAnimationField`). A class's rig
  and clip list are loaded lazily, only once an instance of it is near and on screen, at most 2 loads at a time.
- Default clip: `pickIdle` over the class animation fields ("Base Idle" first) and the model's clips. It now treats `_` as a
  word break and skips death/cower/hit idles (`idle_base` beats `death_idle_forward`).
- Cost control (`scene/animplan.ts`, pure): at most 48 instances animate, within `animRange(drawDistance)` (a quarter of the draw
  distance, 40..600 scene units); off-screen ones are frozen and revert to their batch copy; re-pose rate falls with
  distance (30/20/12/8 Hz); a 5 ms pose budget per frame; selection is re-evaluated every 5th frame. Clips are decoded once
  (one promise per clip) and materials are shared per class (`SkinnedModel` takes shared materials). At most 96 skinned meshes
  are kept allocated. Each instance starts at a phase derived from its id so crowds do not move in lockstep.
- Picking and highlight: `ModelLayer.pick` also raycasts the animated meshes; the instanced outline skips animated objects
  (box outline stays) and the layer draws a tinted overlay mesh that shares the skinned geometry.
- UI: `animate` view option (Display > Play animations, persisted, default on). Details panel gets a "Viewport animation"
  section (open by default) with a clip picker for the selected instance (Default, class animations, all model clips). The
  override is view state, not part of the document, and is reset when a level loads.

## Tests

`packages/app/test/animlayer.test.ts`: scheduling rules, clock looping, the view option, the layer with a synthetic rig
(idle on near instances, hidden batch copy, shared clip loads, per-instance override, disable and re-enable, off-screen and
far pause, ray pick) and `pickIdle`.

## Browser proof (real ISO, Playwright own chromium, production build, software GL)

See the result list in the final report; screenshots are under `build/anim-viewport/` (git-ignored, game content).
Software GL runs at 2-8 fps so frame timing was not measured; the pose clock advances by wall time.

## Limits

- Classes whose model is not rigged, or whose clips do not fit the rig (joint count differs), stay in bind pose.
- Animated meshes are not instanced: very dense crowds beyond 48 stay frozen.
- Rotation order and the model-space facing of skinned meshes follow the static batch (same instance matrix).
