# Issue #1 shadow isolation

Reporter evidence inspected: both `build/issue-review/shadows-*.png` images and `issue-1.log`.
The second image shows two distinct humanoid silhouettes; the first additionally shows a round cliff patch.
These are separate symptoms until an isolation capture attributes them.

## Source findings

* Native character silhouette generation runs through `0x1ab890`, including a separate camera/viewport and nested
  geometry submissions. Existing selected-avatar substitution correctly acts before that generation. It must remain.
* Both renderer backends composite modern ray shading over world color after normal guest submissions. The guest
  projector cannot know whether that same frame's modern scene/camera/depth preparation or submission will succeed.
  Suppression based on a previous successful epoch creates missing native shadows on a later failed epoch.
* The resident model export happens at world entry, before this frame's character projector calls. Using those
  calls as an ownership list for that already-exported scene also introduces a frame of lag.
* The current camera shell heuristic and the exporter backdrop lifetime tracker predate this investigation. They
  are not proof that the reported round patch is an unfiltered shell. No shape-based exclusions were broadened.
* The reporter log's first three printed ray passes say `shadows=0` / `shadows=off`; there is no later shadow-state
  line in the supplied log. The screenshot's exact toggle state cannot be established from this log alone.

## Diagnostic added

`DRAKAN_NATIVE_CHARACTER_SHADOWS=0` skips the identified full native character projector on its entry PC only,
logging once when active. Default behavior and selected-avatar substitution are preserved. This opt-in isolation
switch is not a production replacement policy. Pair it with `GSR_RAY_SHADOWS=0/1` for a 2x2 comparison.
The normally enabled multiplayer hooks must remain enabled for this projector hook.

A scratch driver under ignored `build/issue-review/shadow-ab/run.py` launches the deployed `dist/DrakanPC` binary
with isolated cards/options and bounded capture recipes. Root owns the combined build/deployment.

## Validation

Pending the combined incremental build and live native/ray isolation. No perceptual fix is claimed from the
source review or diagnostic alone. A safe replacement must preserve native output on the same frame when the
modern pass cannot render, and must distinguish silhouette texture generation from final receiver blending.

## First deployed 2x2 capture (2026-11-04)

Root's combined deployment SHA256 was `19EBEDA6A3E876836E38D2455B90CE59F0C0DFAE096CEDF24E4EAA56194CE31E`.
All four sequential direct-dist runs exited zero under `build/issue-review/shadow-ab/deployed-d3d11-nativeN-rayN/`.
The ray-on logs confirm shadow evaluation; native-off logs confirm the exact projector diagnostic fired.
The RAM capture reports native shadow mode 2, and the resident scene has 483 layers / 168889 triangles (Surdana).
Each run used its own cards/options and the same input recipe. Two earlier ray-only baselines are separate in the
same parent directory, without the `deployed-` prefix.

The first downward mouse input was too shallow and this grassy initial position does not expose the reporter's
strong overlapping silhouettes. The four images do **not** yet reproduce or perceptually close either reported
shadow symptom. The native-disabled images differ slightly, but idle animation/timing also differ; pixel changes
alone do not establish the native silhouette's location. Further evidence needs a sunlit loaded-save location,
held weapon and a steeper ground view. Other user/parallel game processes appeared during these runs; they were
not terminated and frame rates are not used as acceptance evidence.

## Bounded replacement implementation (opt-in pending attribution)

`DRAKAN_NATIVE_CHARACTER_SHADOWS=auto` selects a shared feedback policy. The scene export carries a feedback object;
the actual DXR/WebGPU native-instance admission loop records object/lifetime-generation pairs with an attempt ticket.
Only successful final composition publishes those admitted pairs. Mere export presence, an enabled setting or
pipeline initialization does not certify a replacement. Excluded, unknown and newly reused objects keep native projection.
The selected-avatar mapping happens first, so eligibility refers to the selected visible body. The automatic policy
is limited to the engine's world phase; inventory and unrelated native glyph shadows keep their existing path.

Both renderers clear feedback on settings changes, scene replacement, reset and skipped/failed world preparation.
Host detach also clears it. Monotonic tickets reject completions/admissions/failures from an older attempt after a
new result or invalidation. Feedback contains only object identities and generations, avoiding ownership cycles.

This bounded implementation observes the last completed renderer result. It does not preserve same-frame fallback:
the activation frame can contain both shadows; after a newly failed pass, native projection returns when the guest
observes that failure (an in-flight frame can briefly lack it). This tradeoff is explicit. Unset still keeps native
projection until a clear native/ray silhouette comparison validates default activation.

Standalone policy regressions cover startup, retained last result during an in-flight pass, missing output/failure,
settings-off/reset/detach, excluded casters, address-generation reuse, scene replacement and out-of-order completion.
Compile/test results and subsequent live attribution will be recorded after the next combined build.

## Focused validation before the second deployment

`ray_shadow_feedback`, `ray_water` and `ray_trace` passed together after the two-phase admission integration.
The backend regressions verify an accepted native caster is published and zero-opacity/excluded camera-shell
(DXR) or singular-transform (WebGPU) instances are not published. The standalone feedback test also exercises
caller-before-worker and worker-before-caller acknowledgement, and publication of an older successful frame
while a newer frame is merely pending. A newer failure/result/reset still rejects late older work.

The source compiled in root's second combined game build. Final visual attribution and automatic-policy gameplay
checks remain separate and pending; a passing structural suite does not demonstrate that the reporter's scene is fixed.

## Ordinary-Rynn projector attribution in the town fixture

Second verified deployment SHA256: `0C08F057E4FD453FC54BA7D34E4FA6E99C32B735253910AB812799A6C8DC3BB4`.
The existing ignored private `build/coop2-fixture-si/mc0` save was copied separately for each run; the source was not
changed. Unlike the initial grass view, the steep town-ground view clearly exposes Rynn's silhouette.

* `sunny-d3d11-native1-ray0/ground.png`: clear native humanoid silhouette on paving.
* `sunny-d3d11-native0-ray0/ground.png`: that silhouette is absent; the exact projector diagnostic is logged.
* `sunny-d3d11-native1-ray1/ground.png`: native silhouette plus modern shading. This particular light arrangement
  does not show a clearly separated second silhouette, so it is not a full reproduction of the reporter's screenshot.

These first three runs exited zero. This establishes that `0x1ab890` owns visible ordinary singleplayer Rynn
projection as well as the previously tested selected-avatar path. The inventory recipe did not arm the dagger in
this old fixture, and the captures are plain Rynn; no held-weapon acceptance claim is made.

The latest user steering asks for a reusable harness using the reporter's configuration, without requiring the
reporter's save or location. The circle agent owns that tracked harness and its configuration/evidence tests.
Automatic replacement remains opt-in pending clear modern-character-shadow retention evidence.

## Automatic-policy live result: default activation rejected

`sunny-d3d11-nativeauto-ray1` also exited zero and logged
`[shadow-policy] modern shadows replace native caster af5d30`. However, its inspected `ground.png` removes the
clear humanoid silhouette seen in forced-native captures. Modern building/world shade remains, but this view does
not demonstrate a distinct retained modern Rynn shadow. Therefore admitted geometry is a structural prerequisite,
not sufficient perceptual replacement evidence. **Do not enable this policy by default from these results.**
The current implementation remains explicitly opt-in `auto`; unset retains native projection.

The four town captures establish native projector ownership but do not establish the reporter's ray-shadow duplicate
cause. They also expose a separate question: why this admitted native character does not produce a clear modern
silhouette on the paving. Material opacity, caster geometry/transform, light visibility and receiver rules remain
candidates; no cause is claimed without additional isolation. The tracked reporter-configuration harness should
preserve both native and modern stages so that missing replacement output cannot be mistaken for a successful fix.

## Experiment withdrawn from production

After review, the automatic feedback policy was removed completely, including backend/runtime integration,
RayOptions tickets, the shared feedback header, its test target, admission integration tests and the `auto` setting.
Only the exact native projector `0/1` isolation switch remains. Pre-existing selected-avatar projector changes were
preserved. Experimental owned code is archived, ignored, under `build/issue-review/experimental-auto-policy/` for
future investigation; it is not part of the delivered runtime.

The modern building's broad shadow may cover the region where Rynn's modern silhouette would otherwise appear,
but this is only an alternative interpretation of the town result, not proof of correct replacement. Default
behavior must not change based on that ambiguity. The final deliverable is the reusable reporter-configuration
shadow-review harness plus diagnostics, with explicit source/evidence limits, rather than an unvalidated shadow
suppression workaround.

Surgical removal verification found no production references to `shadowFeedback`, `shadowSerial`,
`RayShadowFeedback`, `RayShadowAttempt`, `rayShadowCasts` or `ray_shadow_feedback`; integration-only files have no
remaining diff. Clang-format and whitespace checks passed for the retained shadow diagnostic/source/test changes.
