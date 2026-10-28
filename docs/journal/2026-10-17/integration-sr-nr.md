# Combined Super Resolution, neural rendering and combat — 17 October 2026

Runtime source: `474fbb6`, reproduced by fork patches 0052–0064 on `4897134`.
Renderer implementation: outer commit `076ef45`.
Renderer integration baseline executable SHA-256:
`F0B84885ACBE981D25A351076FBF9B5A6B6F81E022C79C72FEF81922848EA3D1`.
The subsequent controlled-actor correction produces release candidate SHA-256
`98C2D216C5F331674111B945CD0EE658D15D13870682E92B714C9D8D9C7DFB1B`.
Deployment identity and backup are recorded separately in `dist/DrakanPC/BUILD_INFO.json`.

## Changes

Actual NGX SR / DLAA can precede DLSS 5 NR. The shared consumer supplies
output-sized color, reverse-Z depth and scaled motion to NR. Alpha is reconstructed
independently of vendor output; the native HUD draws after both features. One
world target reserves the pipeline before reduced drawing, with spatial recovery
for failed prerequisites/evaluations. Model histories advance only on accepted
samples. Feature release precedes session shutdown.

Object identity includes engine owner, pool generation, mesh, part and pass.
Immutable metadata accompanies copied VIF jobs. Verified indices/UVs and actual
projected output validate vertex correspondence. A separate coverage/depth-matched
GPU pass produces perspective-correct motion; unknown, clipped, ambiguous or
mixed-transparency coverage falls back conservatively.

A bounded producer/window-thread handoff prevents a later ordinary frame from
overwriting a completed neural frame before presentation. ImGui and Present stay
on the host thread. Native HUD preview cameras preserve the real world-camera
anchor; actual GUI changes, cuts, projection changes and stale gaps still reset it.

The signed SR DLL is the supplied 310.8.0 runtime, matching NR 310.8.0 / SL 2.13.0.
The linked public SR SDK is 310.9.16. Mixing its newer SR DLL with this NR bundle
rendered successfully but intermittently failed full NR reinitialization, so the
matching bundle is the selected local runtime. No proprietary binaries are tracked.

Arokh keyboard context now follows the native controlled actor/resource class,
instead of the last optional camera-look callback. Native gameplay/camera/script
guards retain menu and cutscene behavior. Focused tests cover 18 synthetic cases
and four actual-RAM checks, including menu Cross versus gameplay Triangle,
native flight axes, settings-overlay suspension and focus-loss neutralization.

## Release checks on 98C2

- `srnr-release-equipped`: 821 SR and 543 NR successes, no evaluation failures,
  four live NR toggles, equipped attacks and Quick Inventory, neutral final input,
  normal quit and unchanged saves. SR continues while NR is off.
- `srnr-release-magic`: selected spell index 0, level 1, resource `0x100d6`;
  mana 100 to 85 and one traced projectile creation followed by native updates.
  Sampled RAM does not capture its trajectory; a second click is not counted as
  another successful cast. Normal quit and unchanged saves.
- `srnr-release-default-off`: direct keyboard Quick Inventory, normal quit,
  unchanged saves. `srnr-release-missing-bundle`: explicitly absent NR directory,
  ordinary presentation and normal quit. Both use this exact release hash.
- Bow selection, ammunition 200 to 199 to 198 and projectile evidence were
  checked on the F0B8 renderer baseline; no later bow-enemy hit is claimed.

## Completed checks

- CPU motion history: 17 checks; WARP color/motion shader raster tests: 8 checks.
  Guide tests include known motion, perspective, alpha/depth occlusion, jitter,
  finite values and all 256 alpha bytes; D3D11 debug reports no warnings/errors.
  CTest: all three focused suites passed. Both NVIDIA modules disabled: build passed.
- Runtime object provenance: 67 checks, including saved-RAM skeletal/topology
  evidence. Public-header ownership/layout probe passed with incomplete Job types.
  Camera policy: 19 checks, retaining the original cut/gap thresholds.
- Isolated matching-runtime GPU consumer: 88 SR evaluations, 80 NR evaluations,
  96 alpha/HUD frames and 67,289,088 numerical motion-component checks. Both
  initialization/processing orders, independent feature recreation, NR toggles,
  DLAA/Quality and resize passed. This is an API/numerical test, not image-quality
  or performance proof.
- `build/t/srnr-final-highres`: actual 3840×2160 window, SR 1364×1364 to
  2048×2048, then actual NR. Complete logs contain at least 378 SR and 379 NR
  successes, no evaluation failures; 366 NR samples preserve history, 13 reset.
  All **1,431 logged world presentations contain neural output**. One bounded
  handoff warning coincides with a high-resolution capture; no ordinary world
  presentation was observed. Script quit and process exit were normal.
- High-resolution object history: 366 consumed worlds with matches,
  11,663 matched batches and 9,839,181 vertex correspondences. GPU readbacks
  distinguish object motion from camera fallback, including 185,500 independently
  moving pixels in one sample. These are not unique mesh vertices or ground-truth
  optical-flow measurements. Named-actor/skeletal matching is not separately counted.
- NR GPU output readback: 12,497,358 / 12,582,912 RGB channels differ from SR
  input, zero nonfinite channels. Loaded module inventory confirms matching 310.8.0.
- `build/t/srnr-final-equipped-toggles`: 819 SR and 544 NR successes, no evaluation
  failures, four scheduled NR off/on transitions, equipped attacks, keyboard/native
  Quick Inventory and neutral final input. Script quit/exit 0; save hashes match.
  A finished 48-kHz stereo mixed-audio WAV is nonempty; no listening review is claimed.

## Combat evidence and limits

The subsequent **98C2 release candidate**, including the controlled-actor fix,
reruns the high-resolution hostile encounter in `build/t/srnr-release-highres`:
**420 SR successes, 420 NR successes, zero evaluation failures; 389 continuing
history samples and 31 resets. All 1,922 logged world presentations use neural
output.** Rynn finishes at HP 50; enemies 83/84 finish at HP 30/29 from 40/40.
Original and fixture-source cards remain unchanged. This supplements the older
F0B8 baseline below; it does not convert every defensive action into a pass.

`build/t/srnr-final-dragon-context` runs the same 98C2 executable with both
features enabled. The native control swap sets `dragon=1`, gameplay input remains
active after camera-look calls stop, and the script quits normally. The forced
fixture does not establish natural quest activation or successful flight/attacks;
see the detailed independent combat report.

The earlier F0B8 high-resolution baseline damages enemy 83 from HP 40 to 24 and enemy
84 from 40 to 38. Rynn takes damage and reaches HP 0 in the last valid snapshot;
this is a death, not an alive-player pass. Lock-on persists through strafe, and
RMB release returns the native pad to neutral. Damage reduction, bow ammunition
and projectile creation, spell mana/projectile creation and a controlled Arokh
mount fixture have separate evidence in [combat-encounters.md](combat-encounters.md).
Their executable identities and remaining gaps must be retained.

Object coverage is partial: unmatched inline groups, CPU-clipped direct-GIF paths,
particles and mixed transparency remain conservative fallback. Every scene,
timed parry/directional combo, Arokh flight/attacks, long runs and device loss are
not established by these checks. No Frame Generation or Ray Reconstruction is
added. Existing ShowCursor/LNK4088 linker warnings remain. Browser support remains
an architectural constraint, not a delivered browser build.
