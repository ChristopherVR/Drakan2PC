# Rynn artifacts after level transitions

## Report and confidence

The user reported character rendering artifacts after transitioning between levels. This investigation found concrete
cache and history lifetime defects that can affect an animated character, but has not yet reproduced the user's exact
transition or established which defect caused that image. A clean synthetic comparison is evidence for the relevant
cache contract, not proof that every reported Rynn artifact is resolved.

The renderer must distinguish a persistent character pose, a new immutable mesh publication, and a different level
reusing guest resource addresses or native IDs. Those identities previously governed several independent caches.

## Capture boundary and content validation

`sceneSink` checked resident geometry identity only every 32 world entries while capture remained enabled. During a
level transition, reused resource IDs could therefore be interpreted in the preceding level's namespace until the next
poll. A cheap resident layout identity is now checked each world entry; a layout change or an explicit camera reset
requests an immediate full geometry identity check. The periodic full check remains for changes that do not alter the
cheap layout stamp. This narrows the stale-level window without copying/hashing the full level every frame; an in-place
change preserving all stamped layout fields can still require the periodic check if no camera reset occurs.

The native model capture fast path previously trusted its mesh/template lookup key even when guest-owned source
metadata at that resource changed. Capture now validates a pose-independent source content signature covering the
resource's face/material/UV and prepared strip/index mapping data. Animated position bytes are deliberately excluded:
a normal pose update keeps the indexed template fast path, while changed topology/material mapping rebuilds it.
Texture snapshot reuse also compares native ID, flags and PBR metadata rather than relying only on sampled image identity.

## Renderer namespace and acceleration structure lifetime

Both ray backends now scope native mesh and corner-weld caches to the actual non-null `SceneModels.level` publication.
A temporary missing model publication is not interpreted as a new level; zero remains a valid real level value.
Clearing the namespace prevents a reused topology/count from inheriting the previous level's positional seam grouping
and smoothed normals. Material and alpha-coverage cache state is invalidated with that namespace. WebGPU rebuilds its
CPU BVHs as needed; DXR retains displaced GPU allocations until their frame-fence retirement point.

DXR also tracks the immutable source used to build a cached BLAS independently of the source used to pack its material
attributes. A matching numeric mesh ID alone no longer proves the acceleration geometry is current. Attribute refresh
must not make stale BLAS geometry appear current. Reuse compares geometry content against the BLAS source; refit/build
updates that source only for the acceleration structure actually produced.

A GPU regression changes geometry while reusing an ID, then changes the level while retaining topology/count but
separating and tilting previously welded corners. Its warm-cache output is compared against independent fresh welding.
This directly exercises the stale-geometry/seam contracts rather than only checking cache counters.

## Temporal lifetime

Resident geometry identity replacement now explicitly invalidates previous-level temporal state in both backends.
D3D11 resets previous camera validity, SR/retained neural history state, object/model motion history and pending model
preparation; WebGPU resets previous TAA camera validity and motion history. Pending temporal worlds are discarded.
The current camera is preserved: `sceneSink` publishes it before resident geometry, so calling the broader cut path
would erase the new level's incoming camera. Same-identity geometry refresh does not force a new temporal cut.

Previously camera reset depended on heuristic GUI/tick-gap/jump/turn/projection changes. Those checks are useful but
are not a level-identity contract. Model-motion matching already rejects different published levels; this correction
also resets the image history that could otherwise survive a fast transition.

## Validation and remaining reproduction

Build, focused test and gameplay results are appended by the coordinating investigation. No successful replay of the
user's exact transition is claimed here. For a live reproduction, distinguish short-lived temporal ghosts from persistent
pose/seam errors: compare the same transition with DLSS disabled and with ray model reuse disabled, then return to the
original settings. Capture the first destination world and a later stationary frame to identify whether the artifact decays.

### Integrated validation

- Runtime build passed (`build/rynn-transition-final-build.log`). Ten focused tests
  passed: ray_trace, ray_water, ray_water_lost_signal, scene_geometry,
  scene_models, model_motion, motion_history, motion_shader,
  temporal_presentation, native_display_commit (6.60 seconds total;
  `build/rynn-transition-tests.log`). The synthetic capture regressions run
  without original-game fixtures, so same-address replacement checks did not skip.
- Standard incremental game build and deployment passed. Build/dist executable
  SHA-256 matched `c83c93cb518e9dc1963a41131a83dc80aa41bd41e25fdcee731482bc04eddb93`.
  Receipts: `build/rynn-transition-game-build.log`, `build/rynn-transition-deploy.log`.
- Fresh-card intro-to-Surdana run with DLSS Quality, scale 4 and all ray effects
  completed in 54 seconds with movement/turn captures; Rynn's captured pose was
  inspected. Evidence: `build/harness/rynn-transition-surdana-20261105/`.
  This is a transition smoke check, not the unspecified user level-to-level repro.
- Formatting checks pass for all touched C++ files. Repository guard reports
  2,313 approved tracked files and zero prohibited files. Unrelated concurrent
  work and already-running games were preserved.
- Fresh-card intro-to-Shadowmire run also completed cleanly in 46 seconds, with early and settled captures (uild/harness/rynn-transition-shadowmire-20261105/). The settled character pose was inspected; pre-existing polygonal water shading remains outside this fix.
