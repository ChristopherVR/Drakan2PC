# Native inventory, input and renderer follow-up

Validated build06 SHA256:
`F40A8EACDB9DD8CCF69334EDC70C162646B70B7A5C680245A1CDB721CD2A64E5`.
Native, combined SR/NR and performance runs completed normally. Map coverage
is limited as described below. Deployment status is recorded in `BUILD_INFO.json`
beside the executable.

## Changes

- Expand the native inventory world's later crop before camera/frustum rebuild.
  This fills the gap beside the left-aligned original inventory without moving
  or stretching Rynn. The categories, artwork and map remain native; no sidebar.
- Constrain enlarged point-sampled UI sprites to the atlas texel cells covered
  by their native raster. Validated multi-sprite batches retain submission order
  with separate bounds. This removes the bright rectangular orb edges in the
  inspected views. `GSR_SPRITE_FOOTPRINT=0` restores legacy coverage.
- Apply RT wrap and CT24/CT16 TEXA/AEM expansion to individual taps before
  bilinear interpolation. Preserve the existing CT32 CLAMP fast path.
- Clear cached input and accumulated mouse displacement at overlay suspension.
  Direct pitch honors native vetoes; stable aim pitch persistence has explicit
  actor, target, camera, control-mode and input-ownership guards.
- Map image markers use fresh native draw bounds, current visibility checks
  and ordinary native selection acknowledgement. Empty/stale/ambiguous hits
  cannot activate an item. Spells/Skills/Journal retain keyboard navigation;
  arbitrary mouse clicks no longer activate their current selection. Native
  art and layout are unchanged. Other Map drawable branches remain unsupported.
- Route F1's Auto/fixed internal-scale changes through the host resize policy.
  Auto follows fitted output dimensions; fixed scales survive subsequent
  fullscreen/resize changes. Requests are applied outside the present callback,
  with resource changes still deferred to a renderer boundary. Invalid or
  minimized output defers Auto calculation until a valid size is available.
  Correct the old height-only description in settings. This does not replace
  the original low-resolution fonts or artwork.

## Validation so far

The combined build06 passed 23/23 native32 and 23/23 neural21 checks, covering
category selection, Equipment clicks, movement, bow and Quick Inventory release.
The latter logged 1,798 successful SR and 1,695 successful NR evaluations. Both
quit normally with exit0; original, source-fixture and private saves were
unchanged. These runs include the Map hooks and live-scale fix, but do not
themselves certify Map marker clicks or interactive F1 scale changes.
Neural21 desktop BMP captures were obscured by another application and are
excluded as visual evidence. Direct renderer PNGs contain the game: inspected
frames include loading, Journal/help and bow gameplay. This is limited visual
coverage, not a whole-game parity check.

Map probes at 21:9 and 32:9 both quit normally with unchanged saves and
selection. Native hooks captured the Palace marker, and direct renderer
images show the Map. The 21:9 raw geometry agrees with the scripted marker
point. The 32:9 run did not reach its scheduled raw draw window. This early
save exposes only Palace: selecting an already selected marker cannot prove
click acknowledgement, state-query success or navigation to another marker.
Treat the live Map result as limited; synthetic CPU tests establish the
bounded selection policy. Other Map drawable branches remain unsupported.

Forced 21:9 and 32:9 fullscreen inventory/map captures show coherent world
coverage beside the original panel. These ratios were exercised on a 16:9
desktop with letterboxing, not physical ultrawide hardware. The live crop log
records offset170, scale0.43333 to0.960787 at21:9.

- Input CPU suite: 3,050 checks; Equipment/categories: 415; Map: 106 synthetic
  checks; Quick Inventory bridge: 10.
- Build05 delayed aiming test: the same target remained selected, Rynn's HP
  fell 100 to 90, the native motion flags pulsed, and pitch stayed unchanged
  through 21 neutral samples. Veto rejection is covered by CPU tests; this
  encounter did not exercise a live native pitch veto. Build05 also passed
  23 native32 checks but predates the Map and live-scale changes.
- New crop policy: 480 checks; existing widescreen policy: 2,489.
- Sprite footprint: 3,928 checks, including intentional borders, reversed axes,
  fractional positions, HD subtexels and unchanged native texel selection.
- RT sampling CPU: 24,919 checks; alpha pipeline: 263,692.
- D3D11 WARP pixels: 368 draws / 23,552 channel checks, including fractional
  RT scales, intentional outlines and HD subtexels. Eight main/motion shader
  variants compile.
- Preliminary 32:9 native run: all 23 category, Equipment, movement and bow
  checks passed with audio enabled and original/fixture/private saves unchanged.
- Preliminary 21:9 SR+NR run: all 23 checks passed, with 1,797 successful SR
  and 1,685 successful NR evaluations. These preliminary runs used build03;
  they do not certify subsequent input or Map changes.
- Texture decoding: two CPU passes each compared 546,252,570 pixels exactly
  against the original decoder across 403,335 cases.
- Rebuilt native display-commit CPU integration test covers Auto-to-fixed,
  fixed-to-Auto, repeated output changes, minimization, pending-request
  replacement and renderer-boundary application. All five policy CTest
  targets passed; this is not an interactive F1/fullscreen test.

## Performance evidence and limits

The fixed4 forward matrix measured 47.53 native, 47.26 SR-requested, 36.45
NR-requested and 35.82 combined-requested world submissions/sec. These are not
scanout measurements or a whole-game guarantee. Quiet matrix logs do not prove
every feature evaluation; see `docs/journal/2026-10-18/runtime-perf-matrix.md` for scope.

A later matched build03/build04 pair measured 48.55/48.38 native worlds/sec
and 38.03/37.95 with SR Quality+NR requested. The decoder's CPU microbenchmark
improvement produced no measurable gameplay gain in this pair. Small negative
differences establish neither improvement nor regression.

Final build06: the quiet fixed4 SR Quality+NR run measured 36.93 worlds/sec.
A separate native run copied the user's 3840x2160 borderless, 16:9, Auto
configuration and texture assets, with audio enabled: Auto selected 8x and
measured 39.31 worlds/sec. The workloads differ and are not a native-versus-NR
A/B. They are scene-specific submission rates, not guaranteed scanout FPS.
All three final profile/quiet runs exited normally with matching executable
hashes and preserved source/private fixtures and configuration.

The profiled combined case spent about 7.70 ms/world in evaluation-submission
CPU completion waits, versus 0.26 ms/world in SDK CPU recording. The fence
includes preceding rendering/SR/transport and NR work; this is not an isolated
NR GPU execution measurement. Current waits and resource lifetimes remain
unchanged. No performance improvement is claimed from this diagnostic work.
See `docs/journal/2026-10-18/data/final-perf-profile-20261018.json` for evidence and limits.

The 30 FPS cap remains removed when uncapped is selected. Guest vblank remains
approximately 60 Hz and timestep accumulation was approximately 0.9997 s/s in the
matrix. Neural rendering adds substantial measured work; texture resolution is
a shared CPU hotspot. Profiling does not justify changing guest time or skipping
coherence barriers.

Broad smoke/shadow/sky parity, complete combos, successful parry, Arokh combat
and exact Order of the Flame feel remain unverified. Original low-resolution
artwork remains. Moving-object guide coverage is partial; Frame Generation and
Ray Reconstruction are not implemented. Linux gameplay and browser runtime
remain future work. Existing compiler/linker warnings remain.
