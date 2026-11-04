# Issue #1 camera-following cliff patch

The first reporter image has a rounded dark patch high on the cliff. The image alone does not identify its
caster or even prove that modern shading produced it. The supplied log only prints the first three ray passes,
all with shadows off. No new geometric exclusion is justified by that evidence.

## Diagnostic separation

`GSR_RAY_DIAGNOSTIC` is a strict, opt-in diagnostic shared by both ray backends. Unset, empty or unrecognized
values add no shader flags. Keep `GSR_RAY_SHADOWS=1` and native scene lighting active when using it:

* `ao`: keep ambient occlusion; direct lights remain unoccluded.
* `direct`: keep direct-light shadows; ambient occlusion returns one.
* `receiver`: show land receiver matching confidence as grayscale, black for no match and white for full match.
  WebGPU uses its existing reduced-resolution receiver pass and upsampling. Water is not a receiver diagnostic.

These modes occupy previously unused `Effects.w` bits 64, 32 and 128 respectively. Normal rendering does not
set them. No scene membership, lighting source or native projector behavior changes.

## Source provenance and replay

The existing `GSR_RAY_SCENE_DUMP` emits RTX1 frame constants and camera-relative triangles, including object and
material identities for resident models. `GSR_RAY_SCENE_DUMP_EPOCH` selects the world epoch (default 200). Pair
it with the same `GSR_TEMPORAL_DUMP_EPOCH`
for depth/image agreement. It now writes `<dump-path>.models.csv` from the same job, containing native model ID,
mesh identity, generation, opacity, full object transform, camera origin, level identity and shell classification. This sidecar
describes the exported snapshot before GPU admission/build success; it does not certify that a model cast a shadow.

The RAM metadata probe in ignored `build/issue-review/shadow-ab/models.cpp` additionally emits controller/flags,
bounds and the current
`cameraShell` classification. Its generation counter is probe-local, not evidence of the original runtime lifetime.

`build/issue-review/shadow-ab/trace_scene.py` tests ray/triangle intersections and emits sorted object/material
candidate hits. It deliberately labels alpha as unverified: RTX1 does not carry alpha textures or per-instance
opacity, so a geometric intersection alone cannot establish the shader's accepted blocker. Join each object key
to the metadata CSV, check the material, and reproduce with the visual diagnostics before changing caster policy.

## Validation

Both focused `gsr_test_ray_water` and `gsr_test_ray_trace` targets compiled in `build/runtime-tests`, including DXR
shader bytecode generation. Focused GPU CTest passed both: `ray_trace` in 3.34 seconds and `ray_water` in 13.05
seconds (16.41 seconds total). Deployed diagnostic captures are pending. Source review established that AO and direct
visibility were previously controlled together by the shadows setting, and that receiver confidence depends on
camera-relative depth matching. Neither observation attributes the reported circle. No fix is claimed yet.

The first deployed direct-Surdana snapshot (`shadow-ab/deployed-d3d11-native1-ray1`) contains 1219 exported
instances. `cameraShell` identifies exactly two 128-triangle resource `b00ec` barrels, objects `af21c0` and `af2400`,
at camera-relative translations `(0,5891.97,0)` and `(0,4711.91,0)`. The only included model within 3000 units is
the player (resource `3`). The ground capture does not reproduce the reported cliff patch. The RAM snapshot is
later than the epoch-200 ray dump, so do not combine their camera coordinates as if they describe one frame.

## Reusable reporter-profile harness

The user's follow-up requested a harness based on the reporter configuration without a save/location dependency.
`scripts/harness/shadow_review.py` and its JSON profile preserve the logged scale 4, aspect 1.778, initial window
1280x960, borderless, uncapped cadence, filtering/deinterlace/flicker settings, German request and foliage strength
0.90. The observed baseline keeps water/reflections enabled and ray shadows disabled. Ray shadows at screenshot
time remain unknown. Isolated cases select native/modern shadows and AO/direct/receiver diagnostics on either
backend. Unknown pack/DLSS/input choices and hardware/content differences remain explicit in the manifest.

The initial tracked-harness D3D baseline on deployed SHA `166ab8be...adab54` exited cleanly and logged matching
renderer settings, German root, uncapped cadence, foliage 0.90 and successful water/reflections with shadows off.
Seven camera screenshots were captured. Epoch 200 preceded prepared gameplay: only entry color was dumped;
the first modern-water pass was around epoch 397. This is missing target-scene provenance, not a successful paired
dump. The harness now defaults its coupled dump epoch to 1000 and checks target model identity and camera X/Z.

The corresponding WebGPU direct diagnostic smoke exited cleanly, captured seven shots, logged matching renderer
settings, native projector disable and successful shadows with one native light and 27756 native-water triangles.
Before the applied-mask receipt existed, diagnostic activation was correctly classified as requested but unverified.
Both backends now emit that receipt for recognized modes after successful submission. No circle reproduction or
fix is established by either execution result.

### Final isolated smoke results

All four selected cases used deployed SHA-256
`1bc35f076f10a9c529e4b862bae1a76d7000fc51e478295a6f896718402d0531`, unchanged before/after every run.
Earlier smokes inherited two local packs and are not the final base-game evidence. The final harness creates empty
per-run mods/overrides directories; all four final cases logged zero packs, zero override files/text lines and 1103
disc files. All exited through scripted quit with seven shots after successful Surdana load and control gating.
Reporter renderer settings, language root, uncapped cadence, foliage 0.90 and borderless initialization were observed.
Initial window settings were 1280x960; the local borderless monitor produced 3840x2160 PNGs, recorded in receipts.

* `build/harness/issue1-reporter-isolated-d3d11`: German observed baseline and receiver diagnostic, both with
  complete backend-supported receipts. Both reached epoch 1000 and produced scene/metadata/prepared-depth pairs;
  per-run resident-model level identities and camera X/Z matched. Baseline constants were `[1,0,1,23]` (water on,
  shadows off); receiver constants were `[1,1,1,151]` and its submitted-mask receipt was 128. Both had reflections
  enabled in constants and 1219 model metadata rows. German resource opens were observed.
* `build/harness/issue1-reporter-isolated-webgpu`: German direct diagnostic, native projection disabled, modern
  shadows active with a native light, mask 32 submitted, successful native-water geometry and German resource
  opens. Backend-supported receipts are complete. Equivalent scene/model/depth dumps and reflection-enable
  receipts are unavailable on this backend and remain explicitly unverified.
* `build/harness/issue1-reporter-isolated-english`: English observed baseline mirrors the original log's effective
  resource language rather than its requested German setting. English root and 15 successful English resource
  opens were observed. The epoch-1000 scene/model/depth pair passed the same per-run checks, with constants
  `[1,0,1,23]` and 1219 metadata rows. Backend-supported receipts are complete.

Final harness policy tests: nine CPU tests passed, Ruff check/format and diff checks passed. After the final C++
diagnostic changes, focused GPU tests `ray_trace` and `ray_water` passed in 1.77 seconds total. The raw depth format
has no independent epoch tag; model identity is a within-run provenance check, not a cross-run asset/language hash.
Native-enable draws are not individually logged, and full visual review remains pending. These runs validate the
review harness and diagnostic receipts; they do not establish a reproduced or fixed circular patch or duplicate.
