# Graphics, pacing and PC controls follow-up — 17 October 2026

## Final candidate

Executable SHA-256:
`77AF378BC98F14757262C902B995C115D37B2DB025DC78CBD565D5A9BE52298E`.
The authored GS fix is integrated as `76d8e18`; nested runtime remains `e20f1a7`.

## Changes

- Corrected the actual inventory health/mana texture addressing. A shifted
  texture crossed a render-target row, but the shader clamped it as a linear
  view. Rejecting that alias uses the existing GS-memory decoder. Actual native
  and SR/NR screenshots confirm the red rectangle outside the health orb is
  gone and the mana swirl is restored. Valid aligned and zero-origin views keep
  their fast path. [Captured proof and regression](inventory-rt-row-alias.md).
- Inventory alpha-only DATE masks use the same page transform/scissor as RGB.
  This separately reproduced defect retains native world/depth-clear behavior.
  [Alpha-mask evidence](inventory-alpha-mask.md).
- GS alpha-test failure passes retain RGB blending; FBA changes stored alpha
  without altering the blending coefficient. CT16 AEM retains opaque black.
- The DLSS sharpness slider affects combined SR/NR output before the HUD,
  preserving alpha/HDR. Sharpness-only changes retain temporal history.
- Completed-world handoffs survive the next pending engine entry only while
  content stays identical. Camera/draw/transfer/display/GUI changes invalidate
  suppression. Guest rendering and vblanks are never skipped. The FPS overlay
  separates World, Present and legacy Flip events from host timer ticks.
- Mouse recapture no longer attacks, suspension clears cached held input,
  nonfinite settings are rejected, and both inversion axes persist through F1.
- All five inventory category icons use captured native bounds. Gaps do not
  activate relative navigation; stale Equipment bounds are invalidated.

## Exact combined build validation

- `followup-release-orb-native`: **23/23**, normal exit in **94.622 seconds**.
- `followup-release-orb-srnr`: **23/23**, normal exit in **124.485 seconds**,
  with SR Quality, four NR off/on transitions, **1,794 successful SR** and
  **1,706 successful NR evaluations**.
- Both runs preserve original, source and private card hashes. RAM checks cover
  all top categories, Equipment cells, empty-cell nonactivation, native dagger
  and bow equip channels, mouse heading/pitch, opposite-motion return, distinct
  forward/strafe displacement, actual bow ammo use and neutral Quick Inventory
  release. Functional runs contain captures and are not benchmarks.
- The selected **seven renderer CTests** pass; the full WARP synthetic renderer
  passes **100/100**. Focused shifted-row regression: **16 checks, three failures
  before; 16/16 after**, including aligned/zero-origin and shadow controls.
- Earlier unchanged input/settings/policy sources passed input **781**,
  inventory category/reset **407**, persisted settings **207**, handoff policy
  **337**, widescreen policy **2,171**, alpha/decode policy **263,692** and eight
  shader compilations. Synthetic sharpening covers 14 cases, all 256 alpha
  values, HDR, edge clamps and repeated resolves; its upscaler is a copy mock,
  not NVIDIA inference. Preparation/source-audit Python tests passed **26**,
  publication guard **five** and capture-audit fixtures **five**.
- An unrestricted renderer CTest invocation on the prior candidate selected two
  optional executables that were not built (`swizzle`, `live`). They were not
  run; the explicit seven-test selection matches CI.

Earlier native encounter evidence reduced enemy HP **40→36→32→28** with dagger
attacks and consumed bow ammo **200→199→198**. Direct mouse movement measured
heading/pitch changes matching configured counts; a separate inverted-axis run
passed. [Input evidence](input-followup.md) and
[category reproduction](inventory-category-validation.md) retain scope/recipes.

## Performance

The earlier matched SR Quality + NR comparison improved from approximately
**20 to 30 unique worlds/s**. The 7F clean follow-up reached **30.387 worlds/s**
in its final six-second sample. These are completed-world submission rates,
not confirmed scanouts or a universal 60 FPS claim. Short-window variation and
one run per build limit the comparison. [Full pacing receipts](pacing-benchmark.md).

The final 77AF clean run exits normally in **72.356 seconds**. Its final three
gameplay windows record **27.98 / 28.49 / 28.38 worlds/s**, approximately
**28.28 worlds/s** weighted across 6.012 seconds. This retains roughly a **42%**
gain over the earlier 19.95 baseline, but is about **7% slower** than the 7F
sample: the correct fallback is not free. These are short, one-run comparisons,
not an isolated causal cost measurement. No captures or verbose DLSS logging
were enabled; functional runs are not used to estimate fallback cost.

## Remaining limits

Exact subjective Order of the Flame parity, successful timed parry, full combos,
Arokh combat and natural late-game progression remain unverified. Flight keeps
its native mouse-to-stick path. Absolute category and Equipment clicking is
verified; other page contents retain relative navigation.

The original gate-guard shadow scene and smoke were not reproduced. Inspected
nearby NPC/Rynn shadows and hair are coherent. A pale shape above the inventory
preview may be the nearby booth awning; world/preview placement remains
unverified. Temporary detached weapon geometry is actively submitted, with its
intended placement unverified. These tests do not establish every scene/effect.
Original low-resolution artwork remains; upscaling does not replace it with new
HD art. Moving-object guide coverage is partial; Frame Generation and Ray
Reconstruction are not implemented. Long runs/device loss remain unverified.
Scripted gameplay runs muted audio; attack-audio behavior was not verified here.

## Build and deployment

The merged Windows Release executable builds with existing u8path/int-to-float
and ShowCursor/FORCE linker warnings. Linux compiler tooling exists; Linux game
rendering and a browser runtime remain future work. A complete clean ISO-wrapper
game link/launch has not been established by this iteration.

The final orb production bundle passed Windows runtime checks and Windows/Linux
compiler jobs in [Actions run 39033690958](https://github.com/ChristopherVR/Drakan2PC/actions/runs/39033690958),
testing production commit `453fda8` (subsequent changes are documentation only).
Compiler artifact uploads were blocked by account storage quota; no compiler
package was uploaded.
Deployment identity, rollback path and preserved-file hashes are recorded in
local `dist/DrakanPC/BUILD_INFO.json`. Existing settings/saves/assets are retained.
