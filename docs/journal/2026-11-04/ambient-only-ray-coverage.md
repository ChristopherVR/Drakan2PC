# Ambient-only ray coverage

The renderer review found that the ray-shadow setting also owns contact ambient
occlusion, but both native backends required at least one direct-light entry
before submitting that pass. An ambient-only world therefore skipped occlusion
even with valid ambient lighting. The shader light-count gates and the WebGPU
low-resolution shadow gate repeated that assumption. Existing ambient-occlusion
fixtures retained a directional light aimed away from the receiver, so they did
not exercise an empty direct-light list.

`SceneLighting::hasContribution()` now recognizes a finite positive ambient
channel as well as direct-light entries. The backend eligibility and status
checks use that shared definition. Invalid, negative and zero ambient values
alone remain ineligible. Direct-light validation remains with each ray backend.
The companion shader changes retain ambient lighting when the direct-light
count is zero and allow the ambient-occlusion receiver path to execute.

The source review also confirmed that the DXR pass is composed into prepared
world color before Super Resolution, with native HUD drawing afterward. The
disabled neural-rendering evaluation is intentional: that bridge was removed
on 28 October, as recorded at the top of the DLSS reference. Re-enabling those
historical stubs would not restore a supported feature.

The eligibility regression covers absent lighting, nonfinite/negative ambient,
positive ambient without lights, and the previous direct-light case. Hardware
shader validation and build results belong to the accompanying integration
receipt. Source eligibility alone does not establish how frequently shipped
levels contain ambient-only lighting, or prove a measured gameplay improvement.

## Native shadow contrast

The shaders linearize the GS scene color with gamma 2.2, but previously multiplied
that radiance by a display-space native lighting ratio. A ratio of 0.2 therefore
retained roughly 0.481 of the displayed brightness. Both shaders now linearize
the ratio as well. This restores authored light attenuation without changing
unoccluded pixels or adding an arbitrary contrast control. WebGPU's low-resolution
water reconstruction reverses this conversion when reconstructing native lighting
units, so its final composite does not apply it twice.

The DXR numerical regression uses an occluder beyond AO range and a known ambient
0.25/direct 1.0 light balance: display input 0.2 must produce 0.04, within 0.001.
The WebGPU fixture independently checks a known ambient/direct ratio. Both GPU
suites also compare an ambient-only scene against the same scene with an
ineffective backward-facing directional light, requiring identical output and
visible contact occlusion. Both suites passed on the local RTX 5080.

## Remaining scope

Ray shading already precedes DLSS SR. The current implementation does not trace
diffuse bounce illumination or general opaque-material reflections; reflections
are restricted to water. It does not implement DLSS Ray Reconstruction. Those
are substantial missing features behind expectations of an across-the-board
transformation, rather than settings that this fix can simply enable. Water
refraction also currently maps world-XZ wave slopes directly into a screen-UV
offset; that warrants a separate camera-rotation fixture before changing it.

Another active task modified ray mesh caching during this review. Its changes
are preserved and are not attributed to this lighting review. The normal
`Build-LocalGame.ps1 -Incremental` deployment completed in that task (log:
`build/perf-still-review/build-ab.log`); deployed and build executable hashes
were checked equal. No files were manually copied into dist.

## Gameplay smoke checks

Fresh-card Surdana runs before and after the fixes completed normally with DLSS
Quality, modern water, reflections and ray shadows enabled. The captures under
`build/harness/rt-review-{before,after}-20261104/` show stronger ground and foliage
shading after the correction. These are separate executions, with animation and
concurrent mesh-cache changes, not an isolated image-quality or performance
benchmark. The updated Shadowmire run also completed normally and was visually
inspected (`build/harness/rt-review-shadowmire-20261104/`); its log confirms water
coverage and three native lights in the evaluated ray pass.

The deployed executable inspected for these runs has SHA-256
`0764075bdf8f339db48159db4c167f6926501d8f40bf2b087a8011e53684a46d`.
The source repository guard passed with zero prohibited tracked files.

The consolidated Release runtime test run completed successfully: all 109 CTest
entries passed or skipped for their documented missing-data requirements, with
zero failures. The DXR and WebGPU ray tests both executed (not skipped), as did
both synthetic renderer suites. Build and CTest logs are
`build/rt-review-tests-build.log` and `build/rt-review-ctest.log`.
