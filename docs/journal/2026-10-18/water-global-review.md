# Native water coverage and resident reflection geometry

18 October 2026, RTX 5080, Windows Release D3D11/D3D12.

This supersedes the water coverage and material limitations of the first
[DLSS/water review](dlss-water-review.md). Full scene reflection coverage
is still incomplete; see the remaining limitations below.

## What changed

Water recognition now uses native layer types and an ordered scope around the
engine's water draw routine, including its CPU clipping path. It does not depend
on a Surdana level name, one vertex tint, or one texture format. The source
snapshot contains all loaded native terrain, ceiling and water layers before
camera culling. The available map exports 483 layers, including 171 water layers,
and 168,889 triangles. It includes 767 water triangles outside the old tint test.

An immutable, owned snapshot travels through the same ordered bridge as the
native draws. Its identity ignores transient lighting flags, survives camera
turns, and changes with level geometry. Bounds and invalid guest pointers fail
closed. Enabling water live requests the snapshot from the running renderer's
actual setting, rather than relying on the startup environment variable.

DXR now receives resident terrain and ceilings outside the camera view, including
on a cold start. Current native water draws supply primary visibility; exporting
every distant water layer as visible water incorrectly painted water into sky.
Captured materials retain decoded texture texels, UVs and per-vertex lighting.
Stable per-layer UV mappings extend observed terrain materials offscreen. The
texture atlas stays resident instead of converting and uploading every texture
on every world submission.

The shader preserves authored water color and opacity, softens shoreline
transitions, reduces wave distortion and reflection roughness, samples real
material texels at ray hits, and rejects refraction through foreground objects.
Original framebuffer alpha remains unchanged.

The final gameplay audit exposed a separate ordering bug: the native EndWorld
marker precedes some CPU-clipped water packets. Resolving immediately left a strip
of original water overwriting the processed shoreline. Resolution now waits
through the perspective world tail until the first native HUD submission.
HUD sprites can retain the fog flag; native UI Q/FST and sprite state distinguish
them from those final water triangles. A display/new-world fallback handles worlds
without a HUD, and discard/reset clears the pending state.

Presentation-backed status from the earlier review remains: evaluation alone
does not report active. The selected display image must carry the matching
processed epoch through successful Present. Menu/loading and bypass are explicit.

## Gameplay and image evidence

Final candidate SHA-256:
`30f5255015eb23c7456d769b69aa6a83c3b7db8bcc25e4c137cbcd25239b2053`.

Evidence is in `build/water-global-review/`:

| Check | Evidence and result |
| --- | --- |
| Snowy riverbank, same-frame water off/on | `verified-water-water-off.png` / `verified-water-water-on.png`, world epoch 200, 1920x1080, SR and NR both off |
| Unamplified water comparison | `water-comparison.png`, crop `(0,300)..(840,840)` from those exact images |
| Shoreline regression | All pixels in `(10,600)..(200,620)` changed by more than one byte; the former unprocessed strip is gone. Top 350 rows remain byte-identical. These are scene-specific image checks, not general quality scores. |
| Pond, same-frame water off/on | `verified-pond-water-off.png` / `verified-pond-water-on.png`, same candidate, SR/NR off |
| DLAA + NR + water/reflections | `verified-combined-nr-off.png` / `verified-combined-nr-on.png`; this isolates NR with DLAA and water held fixed, not all-DLSS-off versus all-DLSS-on |
| Actual presented image | `verified-swapchain.raw` / `.png`; 1920x1080, evaluated epoch 200, `differingBytes=0 saved=1` |
| Live activation | `t/global-water-live-pond/`: started with water off, enabled through F1, exported resident geometry and evaluated DXR without restart; `live-water-enabled.png` is the native window capture |

The final stationary runs exited normally: riverbank water 28.18 seconds, combined
DLAA/NR 32.26 seconds, pond water 36.21 seconds. Live activation ran 110.46 seconds
and was closed normally through its private window. Original, fixture-source and
private memory card hashes remained unchanged. Each harness manifest records the
exact tested executable, configuration and script.

All locations above use the available Surdana save. The snowy riverbank is not
evidence of visiting another map. The user has no later-area saves. A proposed
cave fixture was rejected because native collision moved the player out of its
intended location. An extended coastal UI attempt reached the native death screen
and was replaced with the pond activation check; it is not stability evidence.

Water comparisons omit late HUD/host overlays from the baseline. Compare world
regions only. Crops are not contrast-amplified. I inspected the final native
window and the actual swapchain readback, not only intermediate render targets.

## Validation

- Hardware DXR test with D3D12 debug layer: actual intersections, native depth
  occlusion, sky rejection, finite RGB, alpha/no-water preservation and jitter
  alignment passed. A reflector wholly outside the camera frustum changed 793
  pixels from a cold start; its checker texture independently changed 793 pixels.
- Native scene export tests: real RAM snapshot plus synthetic constant-height
  layers, holes, water classification, immutable ownership, stable identity,
  material projection, bounds and replacement passed.
- Presentation status transition tests passed.
- Broader native renderer regression: 180 checks, zero failures. Hardware guide
  tests passed with no D3D11 debug warnings/errors.
- Final renderer build with both DLSS and neural features compiled out passed.
- Runtime patch refreshed surgically and applied cleanly to pinned upstream
  `eb8688b2fd191129f103a39543002c005b33409c`; all six refreshed runtime sources
  matched after application. Unrelated patch sections were preserved.

The independent runtime build compiles current native runtime, renderer, UI and
input sources. It reuses the generated-game archive from the complete validated
build, avoiding interference with other active build directories. It does not
revert concurrent inventory, HUD, controls, font or subtitle fixes.

## Remaining limitations

This fixes resident **terrain and ceiling** omissions. Unseen prop/model geometry
is not yet exported comprehensively; foliage, alpha-tested and transparent
materials have partial coverage. Objects currently submitted by the native game
can contribute when eligible. Offscreen terrain whose texture has never been
observed still uses baked vertex color. Per-layer material projection is an
approximation, not a complete native material export.

Reflections are an added DXR pass, not DLSS Ray Reconstruction or frame generation.
Sky misses use visible sky samples or a low-frequency estimate, not a complete
authored environment map. Lighting and most source assets remain original. The
pond and snowy river improve visibly, but this is not a full PBR remaster or an
artifact-free result. Geometry limits are 450,000 native triangles and 500,000
combined ray triangles; oversized input bypasses. Other levels, long sessions,
device loss and complete prop coverage remain unverified.

Deployment details and protected-file hashes are recorded in
`build/water-global-review/deployment.json` and the existing distribution's
`BUILD_INFO.json`. Restart the game to load the replacement executable.
The saved distribution settings had all four effects off. Deployment restores
`dlss=quality`, `dlss_neural=1`, `modern_water=1` and `ray_reflections=1` for the next
launch. Other settings and 3,015 protected distribution files remain unchanged;
the previous executable, INI and build receipt are backed up together.
