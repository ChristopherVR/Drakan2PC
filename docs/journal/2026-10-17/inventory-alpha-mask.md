# Inventory alpha-only mask regression

Base `00a0f85`, isolated branch `graphics-orb-alpha`. Inventory mapping was
gated on RGB writes twice. An alpha-only DATE mask (FRAME.FBMSK=00ffffff,
writeMask8) stayed native while its RGB consumer moved through the centred0.75
page map. Actual GIF commands and WARP pixel/VRAM-alpha readback prove this
geometry/scissor disagreement.

## Evidence

`gsr_test_synthetic --warp --scale 1 --inventory-alpha-only`:

- Before: **22 checks,8 failed, exit1** on the unmodified base backend.
- After: **22 checks,0 failed, exit0** with the targeted backend fix.
- Five modes: mapped DATE/DATM1, identity page, live world with inventory
  metadata, depth-only native clear, mapped inverse DATE/DATM0.
- Tight mask scissor native48..80 maps to100..124. Corrected mask alpha at112
  is128; exterior72 remains0. RGB now fills112 red200 and leaves72 blue200;
  before, those colour outcomes were reversed. Samples96/128 additionally
  enforce the transformed scissor, not merely transformed geometry bounds.
- Public queued HUD settings are applied through `vsync(false)` before each
  scene, ensuring the test exercises0.75 mapping rather than default identity.

## Scope and integration

Page geometry/scissor now recognizes any RGBA framebuffer write, including
alpha-only. Depth-only clears still have writeMask0 and remain native. Canvas
clearing remains RGB-only. The outer HUD gate admits alpha-only geometry only
for an authoritative inventory page; gameplay/legacy eligibility is unchanged.
World and offscreen-width gates are unchanged: width128 shadow producers cannot
qualify as a page. Tests also join the normal synthetic suite.

Private logs: `build/graphics-check/inventory-alpha-before.log` and
`inventory-alpha-after.log`. Tracked summary:
`docs/journal/2026-10-17/data/inventory-alpha-mask-regression.json`.

Cherry-pick this source commit and rebuild GS. The focused CLI can run in CI
with WARP. No full game was run with the fix here; main owns native, SR/NR and
pacing validation. Synthetic proof establishes this mask defect. Removal of
the photographed inventory orb spill awaits the actual game screenshot check.
Shared stage and dist were not modified.

## Actual game result: photographed spill remains

Main integrated the source as `e914d91`; executable SHA256
`7F80774EEBE4634BFEFD7DF60B5E674C9E05BD6EAAC68A1F1E2E2A0EC292F387`.
Read-only inspection of
`build/t/followup-release-alpha-native/render_2500.png` confirms that the red
rectangle still extends beyond the health orb's top/right circumference.
The large pale mesh above the Rynn preview also remains. Neither photographed
issue is fixed by this patch. The passing synthetic test proves a separate
inventory alpha-mask mapping defect, not the cause/removal of these artifacts.

Both artifacts are visible in the earlier native/NR-off2AE7FC inventory capture;
this inspection does not demonstrate a new release regression. Hair looks
coherent in the inspected views; smoke was not exercised.

Read-only evidence inventory:

- No inventory GIF recording found among the three available `gscap*.bin`
  files: old small black capture, gameplay baseline, nearby-hostile gameplay.
- Historical RGB/alpha RT dumps in `build/recomp_inv/bin/rtd`, including
  `rtdump_56901.png`/`rtdump_56901_alpha.png`, show a different inventory-help
  popup scene with a clean orb. They are not a matched failing-frame capture.
- `inventory-drawtrace/runtime.log` frame2500 contains depth-only batches but
  no identified orb DATE/alpha-only-mask pass. It logged no vertices, so the
  orb's exact fill, mask, source texture and producer cannot be isolated.

No further renderer fix is justified by those assets alone. For later targeted
diagnosis, record three GIF frames during the actual Equipment page matching
PNG2500; set `PS2X_GS_CAPTURE_FROM` in **seconds**, using measured run timing and
confirm GUI21. Also record only GS frame2500 with `GSR_DRAWLOG=2500-2500` and
`GSR_DRAWLOGV=768`. Identify the orb fill/ring and depth-only mask draws first,
then dump only those RGB/alpha producers/consumers. Do not use all-draw RT PNG
dumping. No additional GPU run was launched here while main's final runs were
active. Actual orb masking and the pale preview mesh remain open defects.
