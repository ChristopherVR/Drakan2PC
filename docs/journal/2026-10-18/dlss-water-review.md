# DLSS presentation and experimental DXR water review

Water coverage/material behavior is superseded by the
[native water and resident reflection geometry follow-up](water-global-review.md),
which also fixes the late native water packet ordering gap.

18 October 2026, RTX 5080, Release Windows D3D11/D3D12 renderer.

The complete current runtime was rebuilt from `tools/PS2Recomp`, including the
current input, menu, font and subtitle changes. Neural processing reaches the
displayed image. Water and reflections required a separate renderer pass; merely
enabling DLSS did not provide either effect.

## Changes

- Presentation status now follows the selected display target's world epoch and
  processed-output tag after successful Present. An earlier evaluation cannot
  make a later bypassed world report active. Menu/loading, missing eligible
  geometry/depth, failed presentation and successful gameplay are distinct.
  F1 exposes a visible runtime-status row refreshed every half second.
- Modern water runs before SR, then neural rendering, then the native HUD. It
  uses reconstructed world positions, animated normals, depth absorption,
  refraction, Fresnel and foam, preserving GS framebuffer alpha.
- Reflections use actual DXR 1.1 inline ray queries over a per-world BLAS/TLAS,
  four stable roughness rays and native-depth occlusion. Hit radiance uses the
  current image where valid, with conservative material-average fallback.
  This is a geometry ray tracing pass, not an SSR-only effect.
- Water and reflection switches persist and apply live at a frame boundary,
  resetting relevant histories/resources. Unsupported capability or failed
  water processing retains the original image. Code defaults keep water off;
  the requested local deployment enables it with ray reflections.

## Matched gameplay evidence

Private copies of the Surdana bow-item save were used. Original, source-fixture
and private-card hashes remained unchanged. Both scripted comparison runs exited
normally; the final status UI run was closed normally with Alt+F4.

The instrumented renderer creates each off/on pair from **the same submitted
world at epoch 200**, with the same presentation shader and fixed water time.
The neural pair keeps SR Quality and water enabled; only neural output differs.
The water pair keeps SR and neural rendering off. These isolate the effects and
are not a benchmark of the entire graphics preset.

The off baseline precedes late native HUD/weapon/host overlays. Therefore image
statistics use only `(0,0)..(1280,420)` and comparison sheets crop the same world
area in each panel. They do not attribute late-overlay differences to DLSS.

| Comparison | Changed world pixels | Mean absolute RGB difference, 0–255 |
| --- | ---: | ---: |
| Neural off/on, SR Quality + water fixed | 534,768 / 537,600 | 6.27158 |
| Original/modern water + DXR, SR/NR off | 40,727 / 537,600 | 0.34655 |

Water differences are confined to `(208,286)..(998,375)` in this world ROI.
Difference counts establish operation, not subjective image quality. The pond
enhancement is modest in this camera view and does not replace its authored mesh.

The final on image, including native HUD, was also read back from the **actual
D3D12 swapchain target after the presentation copy**. All RGBA bytes matched the
finished D3D11 presentation image: **0 differing bytes at 1280×720**. The actual
game window was visually inspected. The final F1 screen correctly reported no
new eligible gameplay world while the menu paused gameplay.

Local evidence under `build/dlss-water-review/`:

- `neural-comparison.png`, `water-comparison.png`: labeled, unamplified 1:1 crops.
- `release-combined-nr-off.png`, `release-combined-nr-on.png`: original captures.
- `release-water-water-off.png`, `release-water-water-on.png`: water captures.
- `release-presented.png`, `release-swapchain.png`, `release-swapchain.raw`:
  final presentation and swapchain audit.
- `ui/release-runtime-status.jpg`: final visible runtime-status row.
- `t/dlss-water-release-{combined,water,status}/`: logs, completion and save checks.
- `comparison-metrics.json`: explicit ROI and image metrics.

## Validation

- Complete `ps2EntryRunner` Release source build; existing ShowCursor/FORCE link
  warnings remain. No older imported generated-game library is used.
- Builds with both DLSS and neural modules disabled also succeed.
- Hardware DXR test with D3D12 debug layer: real reflection contribution (1,107
  changed pixels), exact one-pixel jitter alignment, depth occlusion, sky
  rejection, finite RGB, exact alpha and no-water output preservation pass.
- Temporal presentation transition tests pass, including stale processing tags,
  bypass, menu, failed Present and zero-epoch cases.
- Hardware guide, motion, alpha and repeated composition checks pass. Settings
  core: 331 checks; settings menu: 125 checks; zero failures.
- Native synthetic renderer regression covers alpha, caches, overlapping RTs,
  page addressing and HUD/layout. Its stale centered inventory test coordinates
  were corrected to the existing left-origin layout; renderer layout was not
  changed by this correction. Final scale results are recorded in the JSON receipt.
- The canonical runtime patch applies cleanly to pinned upstream
  `eb8688b2fd191129f103a39543002c005b33409c`; bridge and settings-host sources match.

## Coverage limits

Water identification currently recognizes the verified Surdana GS material
signature and horizontal geometry. Other levels/materials need evidence and
classification before claiming whole-game water replacement. The AS contains
only geometry submitted by the game; culled/offscreen geometry is unavailable,
and alpha-tested/transparent coverage is incomplete. This can produce missing
or approximate reflections. Four rays soften the result but do not create a
complete PBR material export or an environment lighting solution.

Moving-object neural guides retain the conservative limitations documented in
`docs/reference/dlss.md`. Device loss, long sessions and whole-game image quality/performance
are not established by these checks. Frame Generation and Ray Reconstruction
are not added. Building the optional DXR pass requires DXC and the NR interop
source; unsupported builds retain ordinary rendering and report availability.

Source/deployment hashes and validation receipts are in
`docs/journal/2026-10-18/data/dlss-water-20261018.json` and the existing deployment
`BUILD_INFO.json`, whose earlier receipts are preserved.
